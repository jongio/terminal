// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "CopilotActivity.h"

namespace winrt::TerminalApp::implementation
{
    static uint64_t currentSystemTime() noexcept
    {
        FILETIME now;
        GetSystemTimePreciseAsFileTime(&now);
        return (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    }

    CopilotProgressTransition CopilotPaneEventState::CaptureProgress(const uint64_t taskbarState)
    {
        std::lock_guard lock{ _mutex };
        return {
            .taskbarState = taskbarState,
            .observedAt = CopilotActivityClock::now(),
            .sequence = ++_progressSequence,
            .focus = { _acknowledgementGeneration, _focused },
        };
    }

    CopilotAttentionTransition CopilotPaneEventState::CaptureAttention()
    {
        std::lock_guard lock{ _mutex };
        return {
            .observedAt = CopilotActivityClock::now(),
            .observedSystemTime = currentSystemTime(),
            .sequence = ++_attentionSequence,
            .focus = { _acknowledgementGeneration, _focused },
        };
    }

    CopilotEventOrdering CopilotPaneEventState::CaptureOrdering() const
    {
        std::lock_guard lock{ _mutex };
        return {
            .progressSequence = _progressSequence,
            .attentionSequence = _attentionSequence,
            .focus = { _acknowledgementGeneration, _focused },
            .observedAt = CopilotActivityClock::now(),
        };
    }

    CopilotFocusState CopilotPaneEventState::SetFocused(const bool focused)
    {
        std::lock_guard lock{ _mutex };
        if (focused)
        {
            ++_acknowledgementGeneration;
        }
        _focused = focused;
        return { _acknowledgementGeneration, _focused };
    }

    CopilotProcessSnapshot::CopilotProcessSnapshot(std::vector<CopilotProcessInfo> processes)
    {
        _processes.reserve(processes.size());
        for (auto&& process : processes)
        {
            const auto processId = process.identity.processId;
            _children[process.parentProcessId].emplace_back(processId);
            _processes.try_emplace(processId, std::move(process));
        }
    }

    CopilotPaneProcessState CopilotProcessSnapshot::Classify(const CopilotProcessIdentity& rootProcess) const
    {
        CopilotPaneProcessState result;
        const auto root = _processes.find(rootProcess.processId);
        if (root == _processes.end())
        {
            return result;
        }
        if (rootProcess.creationTime != 0 && root->second.identity.creationTime != rootProcess.creationTime)
        {
            return result;
        }

        result.rootProcess = root->second.identity;

        struct PendingProcess
        {
            uint32_t processId;
            size_t depth;
            uint64_t creationTimeLowerBound;
        };

        std::vector<PendingProcess> pending;
        pending.emplace_back(PendingProcess{ rootProcess.processId, 0, root->second.identity.creationTime });
        std::unordered_set<uint32_t> visited;
        visited.reserve(_processes.size());

        while (!pending.empty())
        {
            const auto [processId, depth, creationTimeLowerBound] = pending.back();
            pending.pop_back();

            if (!visited.emplace(processId).second)
            {
                continue;
            }

            const auto process = _processes.find(processId);
            if (process == _processes.end())
            {
                continue;
            }

            if (_wcsicmp(process->second.executableName.c_str(), L"copilot.exe") == 0)
            {
                result.copilotProcesses.emplace_back(process->second.identity);
            }

            if (depth >= MaxTraversalDepth)
            {
                continue;
            }

            if (const auto children = _children.find(processId); children != _children.end())
            {
                for (const auto childProcessId : children->second)
                {
                    const auto child = _processes.find(childProcessId);
                    if (child == _processes.end() ||
                        (creationTimeLowerBound != 0 &&
                         child->second.identity.creationTime != 0 &&
                         child->second.identity.creationTime < creationTimeLowerBound))
                    {
                        continue;
                    }

                    auto nextCreationTimeLowerBound = creationTimeLowerBound;
                    if (child->second.identity.creationTime != 0)
                    {
                        nextCreationTimeLowerBound = std::max(nextCreationTimeLowerBound, child->second.identity.creationTime);
                    }
                    pending.emplace_back(PendingProcess{ childProcessId, depth + 1, nextCreationTimeLowerBound });
                }
            }
        }

        std::sort(result.copilotProcesses.begin(), result.copilotProcesses.end(), [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.processId, lhs.creationTime) < std::tie(rhs.processId, rhs.creationTime);
        });
        return result;
    }

    void CopilotPaneActivity::SetFocused(const CopilotFocusState& focus) noexcept
    {
        _focusAcknowledgementGeneration = focus.acknowledgementGeneration;
        _focused = focus.focused;
        if (focus.focused)
        {
            _attentionLatched = false;
            _attentionGeneration = 0;
        }
    }

    void CopilotPaneActivity::UpdateProcesses(const CopilotProcessTransition& transition) noexcept
    {
        const auto& processState = transition.processState;
        const auto progressSequence = transition.preSnapshotOrdering.progressSequence;
        const auto preSnapshotAttentionSequence = transition.preSnapshotOrdering.attentionSequence;
        const auto postSnapshotAttentionSequence = transition.postSnapshotOrdering.attentionSequence;
        bool processGenerationStarted{ false };
        const auto startProcessGeneration = [&]() noexcept {
            ++_processGeneration;
            _progressSequenceBarrier = progressSequence;
            _lastProgressSequence = std::max(_lastProgressSequence, progressSequence);
            _progressActive = false;
            _workObserved = false;
            processGenerationStarted = true;
        };

        if (processState.rootProcess.processId != 0 &&
            _rootProcess.processId != 0 &&
            processState.rootProcess != _rootProcess)
        {
            _resetActivity();
        }

        if (processState.rootProcess.processId != 0)
        {
            _rootProcess = processState.rootProcess;
        }

        const auto processPresent = !processState.copilotProcesses.empty();
        const auto processSetReplaced = _processPresent &&
                                        processPresent &&
                                        std::none_of(_copilotProcesses.begin(), _copilotProcesses.end(), [&](const auto& existing) {
                                            return std::find(processState.copilotProcesses.begin(), processState.copilotProcesses.end(), existing) != processState.copilotProcesses.end();
                                        });

        if (!_processPresent && processPresent)
        {
            startProcessGeneration();
        }
        else if (processSetReplaced)
        {
            _completeWork(transition.postSnapshotOrdering.focus);
            startProcessGeneration();
        }
        else if (_processPresent && !processPresent)
        {
            _completeWork(transition.postSnapshotOrdering.focus);
            _processAttentionEndPreSequence = preSnapshotAttentionSequence;
        }

        _processPresent = processPresent;
        _copilotProcesses = processState.copilotProcesses;
        if (processPresent && processGenerationStarted)
        {
            _processAttentionPreSequence = preSnapshotAttentionSequence;
            _processAttentionPostSequence = postSnapshotAttentionSequence;
            _processAttentionEndPreSequence = 0;
            _replayProgressAfter(progressSequence);
            _resolveAttentionForPositiveObservation(preSnapshotAttentionSequence, postSnapshotAttentionSequence);
        }
        else if (processPresent)
        {
            _processAttentionPreSequence = preSnapshotAttentionSequence;
            _processAttentionPostSequence = postSnapshotAttentionSequence;
            _resolveAttentionForPositiveObservation(preSnapshotAttentionSequence, postSnapshotAttentionSequence);
        }
        else if (!processPresent)
        {
            _resolveAttentionForNegativeObservation(preSnapshotAttentionSequence, postSnapshotAttentionSequence);
        }
    }

    bool CopilotPaneActivity::UpdateProgress(const CopilotProgressTransition& transition) noexcept
    {
        if (transition.sequence <= _lastProgressSequence)
        {
            return false;
        }
        _lastProgressSequence = transition.sequence;

        if (transition.sequence <= _progressSequenceBarrier)
        {
            return false;
        }

        _progressHistory.emplace_back(transition);
        if (_progressHistory.size() > ProgressHistoryLimit)
        {
            _progressHistory.pop_front();
        }

        if (!_processPresent)
        {
            return false;
        }

        _applyProgress(transition);
        return true;
    }

    CopilotAttentionDisposition CopilotPaneActivity::UpdateAttention(const CopilotAttentionTransition& transition) noexcept
    {
        if (transition.sequence <= _lastAttentionSequence)
        {
            return CopilotAttentionDisposition::Ignored;
        }
        _lastAttentionSequence = transition.sequence;

        if (_processPresent)
        {
            if (transition.sequence <= _processAttentionPreSequence)
            {
                if (_copilotExistedAt(transition))
                {
                    _applyAttention(transition);
                    return CopilotAttentionDisposition::Applied;
                }
                return CopilotAttentionDisposition::Ignored;
            }

            _attentionHistory.emplace_back(transition);
            if (_attentionHistory.size() > AttentionHistoryLimit)
            {
                _attentionHistory.pop_front();
            }
            return CopilotAttentionDisposition::Pending;
        }

        if (_processAttentionEndPreSequence != 0 &&
            transition.sequence > _processAttentionPostSequence &&
            transition.sequence <= _processAttentionEndPreSequence)
        {
            _applyAttention(transition);
            return CopilotAttentionDisposition::Applied;
        }

        if (transition.sequence <= _latestNegativeAttentionPreSequence)
        {
            return CopilotAttentionDisposition::Ignored;
        }

        _attentionHistory.emplace_back(transition);
        if (_attentionHistory.size() > AttentionHistoryLimit)
        {
            _attentionHistory.pop_front();
        }
        return CopilotAttentionDisposition::Pending;
    }

    void CopilotPaneActivity::ExpireWorkingLease(const Clock::time_point now) noexcept
    {
        if (_progressActive && now - _lastProgressRefresh >= WorkingLeaseDuration)
        {
            _completeWork({ _focusAcknowledgementGeneration, _focused });
        }
    }

    void CopilotPaneActivity::Reset(const CopilotEventOrdering& ordering) noexcept
    {
        _rootProcess = {};
        _copilotProcesses.clear();
        _progressHistory.clear();
        _attentionHistory.clear();
        _processGeneration = 0;
        _progressSequenceBarrier = ordering.progressSequence;
        _lastProgressSequence = ordering.progressSequence;
        _lastAttentionSequence = ordering.attentionSequence;
        _latestNegativeAttentionPreSequence = ordering.attentionSequence;
        _processAttentionPreSequence = 0;
        _processAttentionPostSequence = 0;
        _processAttentionEndPreSequence = 0;
        SetFocused(ordering.focus);
        _resetActivity();
    }

    CopilotActivityState CopilotPaneActivity::State() const noexcept
    {
        if (_attentionLatched)
        {
            return CopilotActivityState::Attention;
        }
        if (_processPresent && _progressActive)
        {
            return CopilotActivityState::Working;
        }
        return CopilotActivityState::None;
    }

    bool CopilotPaneActivity::HasActiveProgress() const noexcept
    {
        return _progressActive;
    }

    std::optional<std::chrono::milliseconds> CopilotPaneActivity::RemainingWorkingLease(const Clock::time_point now) const noexcept
    {
        if (!_progressActive)
        {
            return std::nullopt;
        }

        const auto remaining = (_lastProgressRefresh + WorkingLeaseDuration) - now;
        if (remaining <= Clock::duration::zero())
        {
            return std::chrono::milliseconds::zero();
        }
        return std::chrono::ceil<std::chrono::milliseconds>(remaining);
    }

    void CopilotPaneActivity::_applyProgress(const CopilotProgressTransition& transition) noexcept
    {
        if (transition.taskbarState == 3)
        {
            _progressActive = true;
            _workObserved = true;
            _lastProgressRefresh = transition.observedAt;
        }
        else if (_progressActive)
        {
            _completeWork(transition.focus);
        }
    }

    void CopilotPaneActivity::_applyAttention(const CopilotAttentionTransition& transition) noexcept
    {
        if (!transition.focus.focused &&
            !_focused &&
            transition.focus.acknowledgementGeneration == _focusAcknowledgementGeneration)
        {
            _attentionLatched = true;
            _attentionGeneration = _processGeneration;
        }
    }

    void CopilotPaneActivity::_completeWork(const CopilotFocusState& focus) noexcept
    {
        if (_workObserved &&
            !focus.focused &&
            !_focused &&
            focus.acknowledgementGeneration == _focusAcknowledgementGeneration)
        {
            _attentionLatched = true;
            _attentionGeneration = _processGeneration;
        }

        _progressActive = false;
        _workObserved = false;
    }

    bool CopilotPaneActivity::_copilotExistedAt(const CopilotAttentionTransition& transition) const noexcept
    {
        return std::any_of(_copilotProcesses.begin(), _copilotProcesses.end(), [&](const auto& process) {
            return process.creationTime != 0 && process.creationTime <= transition.observedSystemTime;
        });
    }

    void CopilotPaneActivity::_resolveAttentionForNegativeObservation(const uint64_t preSnapshotAttentionSequence,
                                                                      const uint64_t /*postSnapshotAttentionSequence*/) noexcept
    {
        _latestNegativeAttentionPreSequence = std::max(_latestNegativeAttentionPreSequence, preSnapshotAttentionSequence);
        auto pending = std::exchange(_attentionHistory, {});
        for (const auto& transition : pending)
        {
            if (_processAttentionEndPreSequence != 0 &&
                transition.sequence > _processAttentionPostSequence &&
                transition.sequence <= _processAttentionEndPreSequence)
            {
                _applyAttention(transition);
            }
            else if (transition.sequence > preSnapshotAttentionSequence)
            {
                _attentionHistory.emplace_back(transition);
            }
        }
    }

    void CopilotPaneActivity::_resolveAttentionForPositiveObservation(const uint64_t preSnapshotAttentionSequence,
                                                                      const uint64_t /*postSnapshotAttentionSequence*/) noexcept
    {
        auto pending = std::exchange(_attentionHistory, {});
        for (const auto& transition : pending)
        {
            if (transition.sequence <= _latestNegativeAttentionPreSequence)
            {
                continue;
            }

            if (transition.sequence <= preSnapshotAttentionSequence)
            {
                if (_copilotExistedAt(transition))
                {
                    _applyAttention(transition);
                }
            }
            else
            {
                _attentionHistory.emplace_back(transition);
            }
        }
    }

    void CopilotPaneActivity::_replayProgressAfter(const uint64_t progressSequence) noexcept
    {
        while (!_progressHistory.empty() && _progressHistory.front().sequence <= progressSequence)
        {
            _progressHistory.pop_front();
        }

        for (const auto& transition : _progressHistory)
        {
            _applyProgress(transition);
        }
    }

    void CopilotPaneActivity::_resetActivity() noexcept
    {
        _copilotProcesses.clear();
        _lastProgressRefresh = {};
        _attentionGeneration = 0;
        _processPresent = false;
        _progressActive = false;
        _workObserved = false;
        _attentionLatched = false;
    }

    CopilotTabActivity AggregateCopilotActivity(const std::span<const CopilotActivityState> states) noexcept
    {
        CopilotTabActivity result;
        uint32_t workingCount{};
        uint32_t attentionCount{};

        for (const auto state : states)
        {
            if (state == CopilotActivityState::Attention)
            {
                ++attentionCount;
            }
            else if (state == CopilotActivityState::Working)
            {
                ++workingCount;
            }
        }

        if (attentionCount != 0)
        {
            result.state = CopilotActivityState::Attention;
            result.paneCount = attentionCount;
        }
        else if (workingCount != 0)
        {
            result.state = CopilotActivityState::Working;
            result.paneCount = workingCount;
        }

        return result;
    }
}
