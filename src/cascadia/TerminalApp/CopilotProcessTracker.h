// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "CopilotActivity.h"

namespace winrt::TerminalApp::implementation
{
    void CancelAndDrainThreadpoolTimer(PTP_TIMER timer) noexcept;

    struct CopilotObservationGenerations
    {
        uint64_t Request() noexcept
        {
            return ++requested;
        }

        uint64_t Begin() const noexcept
        {
            return requested;
        }

        void Complete(const uint64_t generation) noexcept
        {
            completed = std::max(completed, generation);
        }

        bool NeedsObservation() const noexcept
        {
            return requested > completed;
        }

        uint64_t requested{};
        uint64_t completed{};
    };

    class CopilotProcessTracker
    {
    public:
        using Callback = std::function<void(CopilotProcessTransition)>;

        class Registration
        {
        public:
            Registration() = default;
            Registration(const Registration&) = delete;
            Registration& operator=(const Registration&) = delete;
            Registration(Registration&& other) noexcept;
            Registration& operator=(Registration&& other) noexcept;
            ~Registration();

            explicit operator bool() const noexcept;
            void Reset() noexcept;

        private:
            friend class CopilotProcessTracker;
            explicit Registration(uint64_t id) noexcept;

            uint64_t _id{};
        };

        static CopilotProcessTracker& Instance();

        Registration Register(const CopilotProcessIdentity& rootProcess,
                              std::shared_ptr<CopilotPaneEventState> eventState,
                              Callback callback);
        void RequestImmediateSample() noexcept;

    private:
        struct TrackedPane
        {
            CopilotProcessIdentity rootProcess;
            std::shared_ptr<CopilotPaneEventState> eventState;
            Callback callback;
            CopilotPaneProcessState lastState;
            uint64_t lastObservationGeneration{};
            bool sampled{};
        };

        CopilotProcessTracker() = default;
        ~CopilotProcessTracker() noexcept;

        static void CALLBACK _timerCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) noexcept;
        static wil::unique_handle _acquireProcessSnapshot();
        static std::vector<CopilotProcessInfo> _enumerateProcesses(HANDLE snapshot);
        void _unregister(uint64_t id) noexcept;
        void _scheduleTimer() noexcept;
        void _sample() noexcept;

        std::mutex _mutex;
        std::unordered_map<uint64_t, TrackedPane> _trackedPanes;
        wil::unique_threadpool_timer _timer;
        std::atomic_flag _sampling = ATOMIC_FLAG_INIT;
        uint64_t _nextId{};
        CopilotObservationGenerations _observationGenerations;
        bool _stopping{};
    };
}
