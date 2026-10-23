// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "winrt/TerminalApp.h"

namespace winrt::TerminalApp::implementation
{
    using CopilotActivityClock = std::chrono::steady_clock;

    struct CopilotProcessIdentity
    {
        uint32_t processId{};
        uint64_t creationTime{};

        bool operator==(const CopilotProcessIdentity&) const noexcept = default;
    };

    struct CopilotProcessInfo
    {
        CopilotProcessIdentity identity;
        uint32_t parentProcessId{};
        std::wstring executableName;
    };

    struct CopilotPaneProcessState
    {
        CopilotProcessIdentity rootProcess;
        std::vector<CopilotProcessIdentity> copilotProcesses;

        bool operator==(const CopilotPaneProcessState&) const noexcept = default;
    };

    struct CopilotFocusState
    {
        uint64_t acknowledgementGeneration{};
        bool focused{};
    };

    struct CopilotEventOrdering
    {
        uint64_t progressSequence{};
        uint64_t attentionSequence{};
        CopilotFocusState focus;
        CopilotActivityClock::time_point observedAt;
    };

    struct CopilotProgressTransition
    {
        uint64_t taskbarState{};
        CopilotActivityClock::time_point observedAt;
        uint64_t sequence{};
        CopilotFocusState focus;
    };

    struct CopilotAttentionTransition
    {
        CopilotActivityClock::time_point observedAt;
        uint64_t observedSystemTime{};
        uint64_t sequence{};
        CopilotFocusState focus;
    };

    struct CopilotProcessTransition
    {
        CopilotPaneProcessState processState;
        CopilotEventOrdering preSnapshotOrdering;
        CopilotEventOrdering postSnapshotOrdering;
    };

    enum class CopilotAttentionDisposition
    {
        Ignored,
        Pending,
        Applied,
    };

    class CopilotPaneEventState
    {
    public:
        CopilotProgressTransition CaptureProgress(uint64_t taskbarState);
        CopilotAttentionTransition CaptureAttention();
        CopilotEventOrdering CaptureOrdering() const;
        CopilotFocusState SetFocused(bool focused);

    private:
        mutable std::mutex _mutex;
        uint64_t _progressSequence{};
        uint64_t _attentionSequence{};
        uint64_t _acknowledgementGeneration{};
        bool _focused{};
    };

    class CopilotProcessSnapshot
    {
    public:
        explicit CopilotProcessSnapshot(std::vector<CopilotProcessInfo> processes);

        CopilotPaneProcessState Classify(const CopilotProcessIdentity& rootProcess) const;

    private:
        static constexpr size_t MaxTraversalDepth{ 64 };

        std::unordered_map<uint32_t, CopilotProcessInfo> _processes;
        std::unordered_map<uint32_t, std::vector<uint32_t>> _children;
    };

    class CopilotPaneActivity
    {
    public:
        using Clock = CopilotActivityClock;
        static constexpr auto WorkingLeaseDuration = std::chrono::seconds{ 15 };

        void SetFocused(const CopilotFocusState& focus) noexcept;
        void UpdateProcesses(const CopilotProcessTransition& transition) noexcept;
        bool UpdateProgress(const CopilotProgressTransition& transition) noexcept;
        CopilotAttentionDisposition UpdateAttention(const CopilotAttentionTransition& transition) noexcept;
        void ExpireWorkingLease(Clock::time_point now) noexcept;
        void Reset(const CopilotEventOrdering& ordering = {}) noexcept;

        CopilotActivityState State() const noexcept;
        bool HasActiveProgress() const noexcept;
        std::optional<std::chrono::milliseconds> RemainingWorkingLease(Clock::time_point now) const noexcept;

    private:
        static constexpr size_t ProgressHistoryLimit{ 32 };
        static constexpr size_t AttentionHistoryLimit{ 32 };

        void _applyProgress(const CopilotProgressTransition& transition) noexcept;
        void _applyAttention(const CopilotAttentionTransition& transition) noexcept;
        void _completeWork(const CopilotFocusState& focus) noexcept;
        bool _copilotExistedAt(const CopilotAttentionTransition& transition) const noexcept;
        void _resolveAttentionForNegativeObservation(uint64_t preSnapshotAttentionSequence, uint64_t postSnapshotAttentionSequence) noexcept;
        void _resolveAttentionForPositiveObservation(uint64_t preSnapshotAttentionSequence, uint64_t postSnapshotAttentionSequence) noexcept;
        void _replayProgressAfter(uint64_t progressSequence) noexcept;
        void _resetActivity() noexcept;

        CopilotProcessIdentity _rootProcess;
        std::vector<CopilotProcessIdentity> _copilotProcesses;
        std::deque<CopilotProgressTransition> _progressHistory;
        std::deque<CopilotAttentionTransition> _attentionHistory;
        Clock::time_point _lastProgressRefresh{};
        uint64_t _processGeneration{};
        uint64_t _attentionGeneration{};
        uint64_t _progressSequenceBarrier{};
        uint64_t _lastProgressSequence{};
        uint64_t _lastAttentionSequence{};
        uint64_t _latestNegativeAttentionPreSequence{};
        uint64_t _processAttentionPreSequence{};
        uint64_t _processAttentionPostSequence{};
        uint64_t _processAttentionEndPreSequence{};
        uint64_t _focusAcknowledgementGeneration{};
        bool _focused{};
        bool _processPresent{};
        bool _progressActive{};
        bool _workObserved{};
        bool _attentionLatched{};
    };

    struct CopilotTabActivity
    {
        CopilotActivityState state{ CopilotActivityState::None };
        uint32_t paneCount{};
    };

    CopilotTabActivity AggregateCopilotActivity(std::span<const CopilotActivityState> states) noexcept;
}
