// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include "../TerminalApp/CopilotActivity.h"
#include "../TerminalApp/CopilotProcessTracker.h"

#include <future>

using namespace WEX::TestExecution;
using namespace winrt::TerminalApp;
using namespace winrt::TerminalApp::implementation;

namespace TerminalAppLocalTests
{
    static CopilotProcessTransition processTransition(CopilotPaneProcessState state,
                                                      const uint64_t progressSequence,
                                                      const CopilotFocusState focus = {},
                                                      const uint64_t attentionSequence = 0)
    {
        const CopilotEventOrdering ordering{
            .progressSequence = progressSequence,
            .attentionSequence = attentionSequence,
            .focus = focus,
            .observedAt = CopilotActivityClock::now(),
        };
        return {
            .processState = std::move(state),
            .preSnapshotOrdering = ordering,
            .postSnapshotOrdering = ordering,
        };
    }

    static CopilotProcessTransition processTransitionWithAttentionBoundaries(CopilotPaneProcessState state,
                                                                             const uint64_t preSnapshotAttentionSequence,
                                                                             const uint64_t postSnapshotAttentionSequence)
    {
        return {
            .processState = std::move(state),
            .preSnapshotOrdering = {
                .attentionSequence = preSnapshotAttentionSequence,
                .observedAt = CopilotActivityClock::now(),
            },
            .postSnapshotOrdering = {
                .attentionSequence = postSnapshotAttentionSequence,
                .observedAt = CopilotActivityClock::now(),
            },
        };
    }

    static CopilotAttentionTransition attentionTransition(const uint64_t sequence,
                                                          const uint64_t observedSystemTime,
                                                          const CopilotFocusState focus = {})
    {
        return {
            .observedAt = CopilotActivityClock::now(),
            .observedSystemTime = observedSystemTime,
            .sequence = sequence,
            .focus = focus,
        };
    }

    static CopilotProgressTransition progressTransition(const uint64_t taskbarState,
                                                        const CopilotActivityClock::time_point observedAt,
                                                        const uint64_t sequence,
                                                        const CopilotFocusState focus = {})
    {
        return {
            .taskbarState = taskbarState,
            .observedAt = observedAt,
            .sequence = sequence,
            .focus = focus,
        };
    }

    class CopilotActivityTests
    {
        TEST_CLASS(CopilotActivityTests);

        TEST_METHOD(FindsDirectCopilotProcess);
        TEST_METHOD(FindsWrappedCopilotProcess);
        TEST_METHOD(IgnoresUnrelatedCopilotProcess);
        TEST_METHOD(RejectsReusedRootProcess);
        TEST_METHOD(RejectsChildOlderThanParent);
        TEST_METHOD(PreservesProcessIdentityWhenCreationTimeIsUnavailable);
        TEST_METHOD(GuardsCyclesAndDepth);
        TEST_METHOD(TracksWorkingAndLeaseExpiry);
        TEST_METHOD(RoundsSubMillisecondLeaseUp);
        TEST_METHOD(StaleLeaseCallbackPreservesRefreshedWork);
        TEST_METHOD(AppliesCapturedProgressTransitions);
        TEST_METHOD(AcceptsProgressAfterCapturedClassificationBarrier);
        TEST_METHOD(HandlesFocusedAndUnfocusedCompletion);
        TEST_METHOD(UsesFocusAtProgressEventTime);
        TEST_METHOD(AcknowledgesQueuedProcessCompletion);
        TEST_METHOD(RequiresFreshProgressForNewInvocation);
        TEST_METHOD(DoesNotCarryWorkingIntoNewInvocation);
        TEST_METHOD(DoesNotCarryWorkingAcrossPidReuse);
        TEST_METHOD(CorrelatesAttentionSignals);
        TEST_METHOD(ReplaysAttentionBeforePositiveClassification);
        TEST_METHOD(ReplaysAttentionAfterSnapshotBoundary);
        TEST_METHOD(KeepsAttentionPendingDuringSnapshotAcquisition);
        TEST_METHOD(RejectsStaleAttentionFromSnapshotAcquisition);
        TEST_METHOD(DefersAttentionAcrossPositiveToNegativeAcquisition);
        TEST_METHOD(RejectsStalePendingAttention);
        TEST_METHOD(AggregatesPaneActivity);
        TEST_METHOD(ClearsOnlyFocusedPaneAttention);
        TEST_METHOD(ConnectionTeardownClearsWorking);
        TEST_METHOD(ResetClearsActivity);
        TEST_METHOD(DrainsRunningProcessTimerCallback);
        TEST_METHOD(RequestsRerunForOverlappingObservation);
        TEST_METHOD(RequestsRerunDuringSamplingHandoff);
    };

    void CopilotActivityTests::FindsDirectCopilotProcess()
    {
        const CopilotProcessSnapshot snapshot{ {
            { { 10, 100 }, 1, L"copilot.exe" },
        } };

        const auto result = snapshot.Classify({ 10, 100 });
        VERIFY_ARE_EQUAL(10u, result.rootProcess.processId);
        VERIFY_ARE_EQUAL(1u, result.copilotProcesses.size());
        VERIFY_ARE_EQUAL(10u, result.copilotProcesses.front().processId);
    }

    void CopilotActivityTests::FindsWrappedCopilotProcess()
    {
        const CopilotProcessSnapshot snapshot{ {
            { { 10, 100 }, 1, L"powershell.exe" },
            { { 11, 110 }, 10, L"wrapper.exe" },
            { { 12, 120 }, 11, L"CoPiLoT.ExE" },
        } };

        const auto result = snapshot.Classify({ 10, 100 });
        VERIFY_ARE_EQUAL(1u, result.copilotProcesses.size());
        VERIFY_ARE_EQUAL(12u, result.copilotProcesses.front().processId);
    }

    void CopilotActivityTests::IgnoresUnrelatedCopilotProcess()
    {
        const CopilotProcessSnapshot snapshot{ {
            { { 10, 100 }, 1, L"powershell.exe" },
            { { 20, 200 }, 1, L"copilot.exe" },
        } };

        VERIFY_IS_TRUE(snapshot.Classify({ 10, 100 }).copilotProcesses.empty());
    }

    void CopilotActivityTests::RejectsReusedRootProcess()
    {
        const CopilotProcessSnapshot snapshot{ {
            { { 10, 200 }, 1, L"powershell.exe" },
            { { 11, 210 }, 10, L"copilot.exe" },
        } };

        VERIFY_IS_TRUE(snapshot.Classify({ 10, 100 }).copilotProcesses.empty());
        VERIFY_ARE_EQUAL(0u, snapshot.Classify({ 10, 100 }).rootProcess.processId);
    }

    void CopilotActivityTests::RejectsChildOlderThanParent()
    {
        const CopilotProcessSnapshot snapshot{ {
            { { 10, 200 }, 1, L"powershell.exe" },
            { { 11, 0 }, 10, L"wrapper.exe" },
            { { 12, 100 }, 11, L"copilot.exe" },
            { { 13, 210 }, 11, L"copilot.exe" },
        } };

        const auto result = snapshot.Classify({ 10, 200 });
        VERIFY_ARE_EQUAL(1u, result.copilotProcesses.size());
        VERIFY_ARE_EQUAL(13u, result.copilotProcesses.front().processId);
    }

    void CopilotActivityTests::PreservesProcessIdentityWhenCreationTimeIsUnavailable()
    {
        const CopilotProcessSnapshot snapshot{ {
            { { 10, 0 }, 1, L"powershell.exe" },
            { { 11, 0 }, 10, L"copilot.exe" },
        } };

        const auto result = snapshot.Classify({ 10, 0 });
        VERIFY_ARE_EQUAL(0ull, result.rootProcess.creationTime);
        VERIFY_ARE_EQUAL(0ull, result.copilotProcesses.front().creationTime);
    }

    void CopilotActivityTests::GuardsCyclesAndDepth()
    {
        std::vector<CopilotProcessInfo> processes{
            { { 1, 1 }, 2, L"shell.exe" },
            { { 2, 2 }, 1, L"wrapper.exe" },
        };
        for (uint32_t processId = 3; processId < 80; ++processId)
        {
            processes.emplace_back(CopilotProcessInfo{ { processId, processId }, processId - 1, processId == 79 ? L"copilot.exe" : L"wrapper.exe" });
        }

        const CopilotProcessSnapshot snapshot{ std::move(processes) };
        VERIFY_IS_TRUE(snapshot.Classify({ 1, 1 }).copilotProcesses.empty());
    }

    void CopilotActivityTests::TracksWorkingAndLeaseExpiry()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateProgress(progressTransition(3, now, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Working, activity.State());

        activity.ExpireWorkingLease(now + CopilotPaneActivity::WorkingLeaseDuration);
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());
    }

    void CopilotActivityTests::RoundsSubMillisecondLeaseUp()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateProgress(progressTransition(3, now, 1));

        const auto remaining = activity.RemainingWorkingLease(now + CopilotPaneActivity::WorkingLeaseDuration - std::chrono::microseconds{ 500 });
        VERIFY_IS_TRUE(remaining.has_value());
        VERIFY_ARE_EQUAL(1ll, remaining->count());

        activity.ExpireWorkingLease(now + CopilotPaneActivity::WorkingLeaseDuration - std::chrono::microseconds{ 500 });
        VERIFY_ARE_EQUAL(CopilotActivityState::Working, activity.State());
    }

    void CopilotActivityTests::StaleLeaseCallbackPreservesRefreshedWork()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateProgress(progressTransition(3, now, 1));
        activity.UpdateProgress(progressTransition(3, now + std::chrono::seconds{ 5 }, 2));

        activity.ExpireWorkingLease(now + CopilotPaneActivity::WorkingLeaseDuration);
        VERIFY_ARE_EQUAL(CopilotActivityState::Working, activity.State());
        VERIFY_ARE_EQUAL(5000ll, activity.RemainingWorkingLease(now + CopilotPaneActivity::WorkingLeaseDuration)->count());

        activity.ExpireWorkingLease(now + CopilotPaneActivity::WorkingLeaseDuration + std::chrono::seconds{ 5 });
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());
    }

    void CopilotActivityTests::AppliesCapturedProgressTransitions()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateProgress(progressTransition(3, now, 1));
        activity.UpdateProgress(progressTransition(0, now + std::chrono::milliseconds{ 1 }, 2));

        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());
    }

    void CopilotActivityTests::AcceptsProgressAfterCapturedClassificationBarrier()
    {
        CopilotPaneEventState eventState;
        CopilotPaneActivity activity;
        eventState.CaptureProgress(0);
        const CopilotProcessTransition process{
            .processState = { { 10, 100 }, { { 11, 110 } } },
            .preSnapshotOrdering = eventState.CaptureOrdering(),
            .postSnapshotOrdering = eventState.CaptureOrdering(),
        };
        const auto progress = eventState.CaptureProgress(3);

        activity.UpdateProgress(progress);
        activity.UpdateProcesses(process);

        VERIFY_IS_TRUE(process.postSnapshotOrdering.observedAt <= progress.observedAt);
        VERIFY_ARE_EQUAL(CopilotActivityState::Working, activity.State());
    }

    void CopilotActivityTests::HandlesFocusedAndUnfocusedCompletion()
    {
        const auto now = CopilotPaneActivity::Clock::now();

        CopilotPaneActivity focused;
        focused.SetFocused({ 1, true });
        focused.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0, { 1, true }));
        focused.UpdateProgress(progressTransition(3, now, 1, { 1, true }));
        focused.UpdateProgress(progressTransition(0, now, 2, { 1, true }));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, focused.State());

        CopilotPaneActivity unfocused;
        unfocused.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        unfocused.UpdateProgress(progressTransition(3, now, 1));
        unfocused.UpdateProcesses(processTransition({ { 10, 100 }, {} }, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, unfocused.State());
    }

    void CopilotActivityTests::UsesFocusAtProgressEventTime()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateProgress(progressTransition(3, now, 1));

        activity.SetFocused({ 1, true });
        const auto focusedCompletion = progressTransition(0, now, 2, { 1, true });
        activity.SetFocused({ 1, false });
        activity.UpdateProgress(focusedCompletion);
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());

        activity.UpdateProgress(progressTransition(3, now, 3, { 1, false }));
        const auto acknowledgedCompletion = progressTransition(0, now, 4, { 1, false });
        activity.SetFocused({ 2, true });
        activity.SetFocused({ 2, false });
        activity.UpdateProgress(acknowledgedCompletion);
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
    }

    void CopilotActivityTests::AcknowledgesQueuedProcessCompletion()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateProgress(progressTransition(3, now, 1));

        const auto completion = processTransition({ { 10, 100 }, {} }, 1);
        activity.SetFocused({ 1, true });
        activity.SetFocused({ 1, false });
        activity.UpdateProcesses(completion);

        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
    }

    void CopilotActivityTests::RequiresFreshProgressForNewInvocation()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProgress(progressTransition(3, now, 1));
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());

        activity.UpdateProgress(progressTransition(3, now + std::chrono::milliseconds{ 1 }, 2));
        VERIFY_ARE_EQUAL(CopilotActivityState::Working, activity.State());
    }

    void CopilotActivityTests::DoesNotCarryWorkingIntoNewInvocation()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.SetFocused({ 1, true });
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0, { 1, true }));
        activity.UpdateProgress(progressTransition(3, now, 1, { 1, true }));
        activity.UpdateProcesses(processTransition({ { 10, 100 }, {} }, 1, { 1, true }));
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 12, 120 } } }, 1, { 1, true }));

        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
    }

    void CopilotActivityTests::DoesNotCarryWorkingAcrossPidReuse()
    {
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.SetFocused({ 1, true });
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0, { 1, true }));
        activity.UpdateProgress(progressTransition(3, now, 1, { 1, true }));
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 120 } } }, 1, { 1, true }));

        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
    }

    void CopilotActivityTests::CorrelatesAttentionSignals()
    {
        CopilotPaneActivity activity;
        activity.UpdateAttention(attentionTransition(1, 50));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());

        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 100 } } }, 0, {}, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());

        activity.UpdateAttention(attentionTransition(2, 110));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 100 } } }, 0, {}, 2));
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());

        activity.SetFocused({ 1, true });
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
    }

    void CopilotActivityTests::ReplaysAttentionBeforePositiveClassification()
    {
        CopilotPaneActivity activity;
        activity.UpdateAttention(attentionTransition(1, 110));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());

        activity.UpdateProcesses(processTransition({ { 10, 90 }, { { 11, 100 } } }, 0, {}, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());
    }

    void CopilotActivityTests::ReplaysAttentionAfterSnapshotBoundary()
    {
        CopilotPaneEventState eventState;
        CopilotPaneActivity activity;
        const CopilotProcessTransition process{
            .processState = { { 10, 90 }, { { 11, 100 } } },
            .preSnapshotOrdering = eventState.CaptureOrdering(),
            .postSnapshotOrdering = eventState.CaptureOrdering(),
        };
        const auto attention = eventState.CaptureAttention();

        activity.UpdateAttention(attention);
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
        activity.UpdateProcesses(process);
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
        activity.UpdateProcesses(processTransition({ { 10, 90 }, { { 11, 100 } } }, 0, {}, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());
    }

    void CopilotActivityTests::KeepsAttentionPendingDuringSnapshotAcquisition()
    {
        CopilotPaneActivity activity;
        activity.UpdateAttention(attentionTransition(1, 110));
        activity.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 90 }, { { 11, 100 } } }, 0, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());

        activity.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 90 }, { { 11, 100 } } }, 1, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());
    }

    void CopilotActivityTests::RejectsStaleAttentionFromSnapshotAcquisition()
    {
        CopilotPaneActivity acquisitionWindow;
        acquisitionWindow.UpdateAttention(attentionTransition(1, 150));
        acquisitionWindow.UpdateProcesses(processTransitionWithAttentionBoundaries({}, 0, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, acquisitionWindow.State());

        acquisitionWindow.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 190 }, { { 11, 200 } } }, 1, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, acquisitionWindow.State());

        CopilotPaneActivity afterAbsence;
        afterAbsence.UpdateProcesses(processTransitionWithAttentionBoundaries({}, 0, 0));
        afterAbsence.UpdateAttention(attentionTransition(1, 150));
        afterAbsence.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 290 }, { { 11, 300 } } }, 1, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, afterAbsence.State());
    }

    void CopilotActivityTests::DefersAttentionAcrossPositiveToNegativeAcquisition()
    {
        CopilotPaneActivity valid;
        valid.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 90 }, { { 11, 100 } } }, 0, 0));
        valid.UpdateAttention(attentionTransition(1, 110));
        valid.UpdateProcesses(processTransitionWithAttentionBoundaries({}, 0, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, valid.State());

        valid.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 90 }, { { 11, 100 } } }, 1, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, valid.State());

        CopilotPaneActivity stale;
        stale.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 90 }, { { 11, 100 } } }, 0, 0));
        stale.UpdateAttention(attentionTransition(1, 110));
        stale.UpdateProcesses(processTransitionWithAttentionBoundaries({}, 0, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, stale.State());

        stale.UpdateProcesses(processTransitionWithAttentionBoundaries({ { 10, 190 }, { { 12, 200 } } }, 1, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, stale.State());
    }

    void CopilotActivityTests::RejectsStalePendingAttention()
    {
        CopilotPaneActivity negativeEvidence;
        negativeEvidence.UpdateAttention(attentionTransition(1, 50));
        negativeEvidence.UpdateProcesses(processTransition({}, 0, {}, 1));
        negativeEvidence.UpdateProcesses(processTransition({ { 10, 90 }, { { 11, 100 } } }, 0, {}, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, negativeEvidence.State());

        CopilotPaneActivity reset;
        reset.UpdateAttention(attentionTransition(1, 110));
        reset.Reset({
            .attentionSequence = 1,
            .observedAt = CopilotActivityClock::now(),
        });
        reset.UpdateProcesses(processTransition({ { 10, 90 }, { { 11, 100 } } }, 0, {}, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, reset.State());

        CopilotPaneActivity laterGeneration;
        laterGeneration.UpdateProcesses(processTransition({ { 10, 90 }, { { 11, 100 } } }, 0, {}, 0));
        laterGeneration.UpdateProcesses(processTransition({ { 10, 90 }, {} }, 0, {}, 1));
        laterGeneration.UpdateProcesses(processTransition({ { 10, 190 }, { { 12, 200 } } }, 0, {}, 2));
        laterGeneration.UpdateAttention(attentionTransition(1, 150));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, laterGeneration.State());
    }

    void CopilotActivityTests::AggregatesPaneActivity()
    {
        const std::array states{
            CopilotActivityState::Working,
            CopilotActivityState::Attention,
            CopilotActivityState::Attention,
        };

        const auto result = AggregateCopilotActivity(states);
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, result.state);
        VERIFY_ARE_EQUAL(2u, result.paneCount);
    }

    void CopilotActivityTests::ClearsOnlyFocusedPaneAttention()
    {
        CopilotPaneActivity first;
        CopilotPaneActivity second;
        first.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        second.UpdateProcesses(processTransition({ { 20, 200 }, { { 21, 210 } } }, 0));
        first.UpdateAttention(attentionTransition(1, 120));
        second.UpdateAttention(attentionTransition(1, 220));
        first.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0, {}, 1));
        second.UpdateProcesses(processTransition({ { 20, 200 }, { { 21, 210 } } }, 0, {}, 1));

        first.SetFocused({ 1, true });
        const std::array states{ first.State(), second.State() };
        const auto result = AggregateCopilotActivity(states);
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, result.state);
        VERIFY_ARE_EQUAL(1u, result.paneCount);
    }

    void CopilotActivityTests::ConnectionTeardownClearsWorking()
    {
        CopilotPaneEventState eventState;
        CopilotPaneActivity activity;
        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateProgress(progressTransition(3, now, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Working, activity.State());

        eventState.CaptureProgress(3);
        activity.Reset(eventState.CaptureOrdering());
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
        VERIFY_IS_FALSE(activity.HasActiveProgress());
    }

    void CopilotActivityTests::ResetClearsActivity()
    {
        CopilotPaneEventState eventState;
        CopilotPaneActivity activity;
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0));
        activity.UpdateAttention(attentionTransition(1, 120));
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 11, 110 } } }, 0, {}, 1));
        VERIFY_ARE_EQUAL(CopilotActivityState::Attention, activity.State());

        eventState.CaptureProgress(3);
        const auto focused = eventState.SetFocused(true);
        activity.Reset(eventState.CaptureOrdering());

        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
        VERIFY_IS_FALSE(activity.HasActiveProgress());

        const auto now = CopilotPaneActivity::Clock::now();
        activity.UpdateProcesses(processTransition({ { 10, 100 }, { { 12, 120 } } }, 1, focused));
        activity.UpdateProgress(progressTransition(3, now, 2, focused));
        activity.UpdateProgress(progressTransition(0, now, 3, focused));
        VERIFY_ARE_EQUAL(CopilotActivityState::None, activity.State());
    }

    void CopilotActivityTests::DrainsRunningProcessTimerCallback()
    {
        struct TimerContext
        {
            wil::unique_event started{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
            wil::unique_event release{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
            std::atomic<bool> completed{};
        } context;

        VERIFY_IS_NOT_NULL(context.started.get());
        VERIFY_IS_NOT_NULL(context.release.get());

        const auto callback = [](PTP_CALLBACK_INSTANCE, void* rawContext, PTP_TIMER) noexcept {
            const auto timerContext = static_cast<TimerContext*>(rawContext);
            SetEvent(timerContext->started.get());
            WaitForSingleObject(timerContext->release.get(), INFINITE);
            timerContext->completed = true;
        };
        wil::unique_threadpool_timer timer{ CreateThreadpoolTimer(callback, &context, nullptr) };
        VERIFY_IS_NOT_NULL(timer.get());
        const auto releaseCallback = wil::scope_exit([&]() noexcept {
            SetEvent(context.release.get());
        });

        LARGE_INTEGER dueTime;
        dueTime.QuadPart = -1;
        FILETIME dueTimeFile{
            .dwLowDateTime = dueTime.LowPart,
            .dwHighDateTime = static_cast<DWORD>(dueTime.HighPart),
        };
        SetThreadpoolTimer(timer.get(), &dueTimeFile, 0, 0);
        VERIFY_ARE_EQUAL(WAIT_OBJECT_0, WaitForSingleObject(context.started.get(), 5000));

        auto drain = std::async(std::launch::async, [&]() {
            CancelAndDrainThreadpoolTimer(timer.get());
        });

        VERIFY_IS_TRUE(drain.wait_for(std::chrono::milliseconds{ 50 }) == std::future_status::timeout);
        SetEvent(context.release.get());
        VERIFY_IS_TRUE(drain.wait_for(std::chrono::seconds{ 5 }) == std::future_status::ready);
        drain.get();
        VERIFY_IS_TRUE(context.completed);
    }

    void CopilotActivityTests::RequestsRerunForOverlappingObservation()
    {
        CopilotObservationGenerations generations;
        VERIFY_ARE_EQUAL(1ull, generations.Request());
        const auto runningGeneration = generations.Begin();

        VERIFY_ARE_EQUAL(2ull, generations.Request());
        generations.Complete(runningGeneration);
        VERIFY_IS_TRUE(generations.NeedsObservation());

        const auto rerunGeneration = generations.Begin();
        VERIFY_ARE_EQUAL(2ull, rerunGeneration);
        generations.Complete(rerunGeneration);
        VERIFY_IS_FALSE(generations.NeedsObservation());
    }

    void CopilotActivityTests::RequestsRerunDuringSamplingHandoff()
    {
        CopilotObservationGenerations generations;
        generations.Request();
        const auto runningGeneration = generations.Begin();
        generations.Complete(runningGeneration);
        VERIFY_IS_FALSE(generations.NeedsObservation());

        generations.Request();
        VERIFY_IS_TRUE(generations.NeedsObservation());

        const auto rerunGeneration = generations.Begin();
        generations.Complete(rerunGeneration);
        VERIFY_IS_FALSE(generations.NeedsObservation());
    }
}
