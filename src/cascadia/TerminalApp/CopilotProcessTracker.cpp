// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "CopilotProcessTracker.h"

#include <tlhelp32.h>

namespace winrt::TerminalApp::implementation
{
    void CancelAndDrainThreadpoolTimer(const PTP_TIMER timer) noexcept
    {
        if (timer)
        {
            SetThreadpoolTimer(timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(timer, TRUE);
        }
    }

    CopilotProcessTracker::Registration::Registration(const uint64_t id) noexcept :
        _id{ id }
    {
    }

    CopilotProcessTracker::Registration::Registration(Registration&& other) noexcept :
        _id{ std::exchange(other._id, 0) }
    {
    }

    CopilotProcessTracker::Registration& CopilotProcessTracker::Registration::operator=(Registration&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            _id = std::exchange(other._id, 0);
        }
        return *this;
    }

    CopilotProcessTracker::Registration::~Registration()
    {
        Reset();
    }

    CopilotProcessTracker::Registration::operator bool() const noexcept
    {
        return _id != 0;
    }

    void CopilotProcessTracker::Registration::Reset() noexcept
    {
        if (_id != 0)
        {
            CopilotProcessTracker::Instance()._unregister(std::exchange(_id, 0));
        }
    }

    CopilotProcessTracker& CopilotProcessTracker::Instance()
    {
        static CopilotProcessTracker instance;
        return instance;
    }

    CopilotProcessTracker::~CopilotProcessTracker() noexcept
    {
        PTP_TIMER timer{};
        {
            std::lock_guard lock{ _mutex };
            _stopping = true;
            timer = _timer.get();
        }

        CancelAndDrainThreadpoolTimer(timer);
    }

    CopilotProcessTracker::Registration CopilotProcessTracker::Register(const CopilotProcessIdentity& rootProcess,
                                                                        std::shared_ptr<CopilotPaneEventState> eventState,
                                                                        Callback callback)
    {
        THROW_HR_IF(E_INVALIDARG, rootProcess.processId == 0 || !eventState || !callback);

        std::lock_guard lock{ _mutex };
        THROW_HR_IF(RO_E_CLOSED, _stopping);
        const auto id = ++_nextId;
        _observationGenerations.Request();
        _trackedPanes.emplace(id, TrackedPane{ rootProcess, std::move(eventState), std::move(callback), {}, 0, false });
        _scheduleTimer();
        return Registration{ id };
    }

    void CopilotProcessTracker::RequestImmediateSample() noexcept
    {
        std::lock_guard lock{ _mutex };
        if (_stopping || _trackedPanes.empty())
        {
            return;
        }

        _observationGenerations.Request();
        _scheduleTimer();
    }

    void CALLBACK CopilotProcessTracker::_timerCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) noexcept
    {
        static_cast<CopilotProcessTracker*>(context)->_sample();
    }

    wil::unique_handle CopilotProcessTracker::_acquireProcessSnapshot()
    {
        const auto rawSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        THROW_LAST_ERROR_IF(rawSnapshot == INVALID_HANDLE_VALUE);
        return wil::unique_handle{ rawSnapshot };
    }

    std::vector<CopilotProcessInfo> CopilotProcessTracker::_enumerateProcesses(const HANDLE snapshot)
    {
        std::vector<CopilotProcessInfo> processes;
        PROCESSENTRY32W entry{ .dwSize = sizeof(PROCESSENTRY32W) };
        if (!Process32FirstW(snapshot, &entry))
        {
            THROW_LAST_ERROR_IF(GetLastError() != ERROR_NO_MORE_FILES);
            return processes;
        }

        do
        {
            uint64_t creationTime{};
            const wil::unique_handle process{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID) };
            if (process)
            {
                FILETIME created;
                FILETIME exited;
                FILETIME kernel;
                FILETIME user;
                if (GetProcessTimes(process.get(), &created, &exited, &kernel, &user))
                {
                    creationTime = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
                }
            }

            processes.emplace_back(CopilotProcessInfo{
                .identity = { entry.th32ProcessID, creationTime },
                .parentProcessId = entry.th32ParentProcessID,
                .executableName = entry.szExeFile,
            });
        } while (Process32NextW(snapshot, &entry));

        THROW_LAST_ERROR_IF(GetLastError() != ERROR_NO_MORE_FILES);
        return processes;
    }

    void CopilotProcessTracker::_unregister(const uint64_t id) noexcept
    {
        std::lock_guard lock{ _mutex };
        _trackedPanes.erase(id);
        if (!_stopping && _trackedPanes.empty() && _timer)
        {
            SetThreadpoolTimer(_timer.get(), nullptr, 0, 0);
        }
    }

    void CopilotProcessTracker::_scheduleTimer() noexcept
    {
        if (_stopping)
        {
            return;
        }

        if (!_timer)
        {
            _timer.reset(CreateThreadpoolTimer(&_timerCallback, this, nullptr));
            LOG_LAST_ERROR_IF_NULL(_timer.get());
        }

        if (_timer)
        {
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = -1;
            FILETIME dueTimeFile{
                .dwLowDateTime = dueTime.LowPart,
                .dwHighDateTime = static_cast<DWORD>(dueTime.HighPart),
            };
            SetThreadpoolTimer(_timer.get(), &dueTimeFile, 2000, 250);
        }
    }

    void CopilotProcessTracker::_sample() noexcept
    {
        if (_sampling.test_and_set())
        {
            return;
        }
        uint64_t observationGeneration{};
        const auto finishSampling = wil::scope_exit([this, &observationGeneration]() noexcept {
            {
                std::lock_guard lock{ _mutex };
                _observationGenerations.Complete(observationGeneration);
            }

            _sampling.clear();

            std::lock_guard lock{ _mutex };
            if (!_stopping && _observationGenerations.NeedsObservation())
            {
                _scheduleTimer();
            }
        });

        try
        {
            const auto started = std::chrono::steady_clock::now();
            struct SnapshotTarget
            {
                uint64_t id;
                CopilotProcessIdentity rootProcess;
                std::shared_ptr<CopilotPaneEventState> eventState;
                uint64_t observationGeneration;
            };

            std::vector<SnapshotTarget> roots;
            {
                std::lock_guard lock{ _mutex };
                if (_stopping)
                {
                    return;
                }

                observationGeneration = _observationGenerations.Begin();
                roots.reserve(_trackedPanes.size());
                for (const auto& [id, pane] : _trackedPanes)
                {
                    roots.emplace_back(SnapshotTarget{ id, pane.rootProcess, pane.eventState, observationGeneration });
                }
            }

            if (roots.empty())
            {
                return;
            }

            std::vector<CopilotEventOrdering> preSnapshotOrderings;
            preSnapshotOrderings.reserve(roots.size());
            for (const auto& target : roots)
            {
                preSnapshotOrderings.emplace_back(target.eventState->CaptureOrdering());
            }

            const auto processSnapshot = _acquireProcessSnapshot();

            std::vector<CopilotEventOrdering> postSnapshotOrderings;
            postSnapshotOrderings.reserve(roots.size());
            for (const auto& target : roots)
            {
                postSnapshotOrderings.emplace_back(target.eventState->CaptureOrdering());
            }

            const CopilotProcessSnapshot snapshot{ _enumerateProcesses(processSnapshot.get()) };
            std::vector<std::pair<Callback, CopilotProcessTransition>> changes;
            changes.reserve(roots.size());

            for (size_t index = 0; index < roots.size(); ++index)
            {
                const auto& [id, rootProcess, eventState, observationGeneration] = roots[index];
                auto state = snapshot.Classify(rootProcess);
                std::lock_guard lock{ _mutex };
                if (const auto pane = _trackedPanes.find(id);
                    pane != _trackedPanes.end() &&
                    (!pane->second.sampled ||
                     pane->second.lastState != state ||
                     pane->second.lastObservationGeneration < observationGeneration))
                {
                    pane->second.lastState = state;
                    pane->second.lastObservationGeneration = observationGeneration;
                    pane->second.sampled = true;
                    changes.emplace_back(pane->second.callback,
                                         CopilotProcessTransition{
                                             std::move(state),
                                             preSnapshotOrderings[index],
                                             postSnapshotOrderings[index],
                                         });
                }
            }

            for (auto&& [callback, transition] : changes)
            {
                callback(std::move(transition));
            }

            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started);
#pragma warning(suppress : 26477 26485 26494 26482 26446)
            TraceLoggingWrite(
                g_hTerminalAppProvider,
                "CopilotProcessSnapshot",
                TraceLoggingDescription("Measures one shared Copilot process classification snapshot"),
                TraceLoggingUInt64(gsl::narrow_cast<uint64_t>(duration.count()), "DurationMicroseconds"),
                TraceLoggingUInt64(gsl::narrow_cast<uint64_t>(roots.size()), "PaneCount"),
                TraceLoggingKeyword(MICROSOFT_KEYWORD_MEASURES),
                TelemetryPrivacyDataTag(PDT_ProductAndServicePerformance));
        }
        CATCH_LOG()
    }
}
