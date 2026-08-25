#include "maomi_wake.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using maomi::WakeDependencies;
using maomi::WakeHandleResult;
using maomi::WakePhase;
using maomi::WakePlaybackResult;
using maomi::WakePlaybackStart;
using maomi::WakePollGate;
using maomi::WakeSequence;

struct FakeRuntime {
    DeviceState state = kDeviceStateIdle;
    WakePlaybackResult playback_result = WakePlaybackResult::kLocalStarted;
    uint32_t playback_id = 42;
    bool official_invoke_accepted = true;
    int stop_upload_calls = 0;
    int play_calls = 0;
    int invoke_calls = 0;
    int restore_calls = 0;
    int cancel_calls = 0;
    int abort_official_calls = 0;
    std::vector<std::string> actions;
    std::vector<maomi::WakeLogEvent> logs;

    WakeDependencies Dependencies() {
        return {
            .stop_voice_upload =
                [this]() {
                    ++stop_upload_calls;
                    actions.emplace_back("stop_upload");
                },
            .start_local_response =
                [this]() {
                    ++play_calls;
                    actions.emplace_back("play_response");
                    return WakePlaybackStart{playback_result, playback_id};
                },
            .cancel_playback =
                [this]() {
                    ++cancel_calls;
                    actions.emplace_back("cancel_playback");
                },
            .invoke_official =
                [this](const std::string& wake_word) {
                    assert(wake_word == "猫咪过来");
                    ++invoke_calls;
                    actions.emplace_back("invoke_official");
                    return official_invoke_accepted;
                },
            .abort_official =
                [this]() {
                    ++abort_official_calls;
                    state = kDeviceStateIdle;
                    actions.emplace_back("abort_official");
                },
            .restore_wake_detection =
                [this]() {
                    ++restore_calls;
                    actions.emplace_back("restore_wake");
                },
            .logger = [this](maomi::WakeLogEvent event,
                             const maomi::WakeSnapshot&) { logs.push_back(event); },
        };
    }
};

void TestLocalResponsePrecedesOfficialAndStopsUpload() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    const auto result = sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 10'000);

    assert(result == WakeHandleResult::kStarted);
    assert(runtime.actions == std::vector<std::string>({"stop_upload", "play_response"}));
    assert(runtime.invoke_calls == 0);
    assert(sequence.GetSnapshot().phase == WakePhase::kPlayingResponse);

    sequence.HandlePlaybackFinished(10'200, kDeviceStateIdle, runtime.playback_id);
    sequence.Poll(10'200 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);
    assert(runtime.actions ==
           std::vector<std::string>({"stop_upload", "play_response", "invoke_official"}));
    assert(sequence.GetSnapshot().phase == WakePhase::kAwaitingOfficial);

    sequence.Poll(10'300, kDeviceStateListening, true);
    assert(sequence.GetSnapshot().phase == WakePhase::kIdle);
    assert(runtime.restore_calls == 0);
}

void TestOfflineResponseRestoresDetection() {
    FakeRuntime runtime;
    runtime.official_invoke_accepted = false;
    WakeSequence sequence(runtime.Dependencies());

    assert(sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 20'000) ==
           WakeHandleResult::kStarted);
    assert(runtime.play_calls == 1);

    sequence.HandlePlaybackFinished(20'200, kDeviceStateIdle, runtime.playback_id);
    sequence.Poll(20'200 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);

    assert(runtime.invoke_calls == 1);
    assert(runtime.restore_calls == 1);
    assert(sequence.GetSnapshot().phase == WakePhase::kIdle);
    assert(!sequence.IsBusy());
}

void TestMissingCustomSoundUsesFallbackOnce() {
    FakeRuntime runtime;
    runtime.playback_result = WakePlaybackResult::kFallbackCompleted;
    runtime.playback_id = 0;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 30'000);
    assert(sequence.GetSnapshot().fallback_count == 1);
    assert(runtime.play_calls == 1);
    assert(runtime.invoke_calls == 1);
    assert(sequence.GetSnapshot().phase == WakePhase::kAwaitingOfficial);
}

void TestUnrelatedPlaybackDrainDoesNotStartOfficialFlow() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 35'000);
    sequence.HandlePlaybackFinished(35'200, kDeviceStateListening, runtime.playback_id);

    assert(runtime.invoke_calls == 0);
    assert(!sequence.IsBusy());
}

void TestWrongPlaybackTicketIsIgnored() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 37'000);
    sequence.HandlePlaybackFinished(37'200, kDeviceStateIdle, runtime.playback_id + 1);

    assert(runtime.invoke_calls == 0);
    assert(sequence.IsBusy());
    assert(sequence.GetSnapshot().phase == WakePhase::kPlayingResponse);
}

void TestPlaybackFailureRecoversWithoutListening() {
    FakeRuntime runtime;
    runtime.playback_result = WakePlaybackResult::kFailed;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 40'000);

    assert(runtime.invoke_calls == 0);
    assert(runtime.restore_calls == 1);
    assert(sequence.GetSnapshot().playback_failure_count == 1);
    assert(sequence.GetSnapshot().phase == WakePhase::kIdle);
}

void TestConnectionFailureBackAtIdleRestoresDetection() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 50'000);
    sequence.HandlePlaybackFinished(50'200, kDeviceStateIdle, runtime.playback_id);
    sequence.Poll(50'200 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);
    sequence.Poll(50'300, kDeviceStateConnecting, true);
    sequence.Poll(51'500, kDeviceStateIdle, true);

    assert(runtime.restore_calls == 1);
    assert(sequence.GetSnapshot().recovery_count == 1);
    assert(sequence.GetSnapshot().phase == WakePhase::kIdle);
}

void TestIllegalStatesPassThroughUntouched() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());
    const std::vector<DeviceState> states = {
        kDeviceStateWifiConfiguring, kDeviceStateActivating, kDeviceStateUpgrading,
        kDeviceStateAudioTesting,    kDeviceStateFatalError,
    };

    for (auto state : states) {
        assert(sequence.HandleWakeWord("猫咪过来", state, 60'000) ==
               WakeHandleResult::kPassThrough);
    }

    assert(runtime.stop_upload_calls == 0);
    assert(runtime.play_calls == 0);
    assert(runtime.invoke_calls == 0);
    assert(runtime.restore_calls == 0);
}

void TestTwoSecondCooldownSuppressesEchoAndRestoresDetection() {
    FakeRuntime runtime;
    runtime.official_invoke_accepted = false;
    WakeSequence sequence(runtime.Dependencies());

    assert(sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 70'000) ==
           WakeHandleResult::kStarted);
    sequence.HandlePlaybackFinished(70'100, kDeviceStateIdle, runtime.playback_id);
    sequence.Poll(70'100 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);

    assert(sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 71'999) ==
           WakeHandleResult::kSuppressed);
    assert(runtime.play_calls == 1);
    assert(runtime.restore_calls == 2);
    assert(sequence.GetSnapshot().duplicate_count == 1);

    assert(sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 72'000) ==
           WakeHandleResult::kStarted);
    assert(runtime.play_calls == 2);
}

void TestStalledPlaybackTimesOutAndClearsWork() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 80'000);
    sequence.Poll(80'000 + maomi::kWakePlaybackTimeoutMs - 1, kDeviceStateIdle, false);
    assert(sequence.IsBusy());

    sequence.Poll(80'000 + maomi::kWakePlaybackTimeoutMs, kDeviceStateIdle, false);
    assert(runtime.cancel_calls == 1);
    assert(runtime.restore_calls == 1);
    assert(runtime.invoke_calls == 0);
    assert(!sequence.IsBusy());
    assert(sequence.GetSnapshot().phase == WakePhase::kIdle);
}

void TestMaximumAcceptedSoundHasTimeoutMargin() {
    static_assert(maomi::kWakePlaybackTimeoutMs > 3000 + maomi::kWakeOutputDrainMs);

    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());
    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 85'000);

    sequence.Poll(88'000, kDeviceStateIdle, false);
    assert(sequence.IsBusy());
    assert(runtime.cancel_calls == 0);
}

void TestPlaybackIdleBeforeCompletionCallbackDoesNotWinRace() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 90'000);
    sequence.Poll(90'100, kDeviceStateIdle, true);

    assert(runtime.invoke_calls == 0);
    assert(runtime.cancel_calls == 0);
    assert(runtime.restore_calls == 0);
    assert(sequence.GetSnapshot().playback_failure_count == 0);
    assert(sequence.IsBusy());

    sequence.HandlePlaybackFinished(90'101, kDeviceStateIdle, runtime.playback_id);
    assert(runtime.invoke_calls == 0);
    sequence.Poll(90'101 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);
    assert(runtime.invoke_calls == 1);
    assert(sequence.GetSnapshot().phase == WakePhase::kAwaitingOfficial);
}

void TestHardwareOutputDrainPrecedesOfficialInvoke() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 92'000);
    sequence.HandlePlaybackFinished(92'100, kDeviceStateIdle, runtime.playback_id);

    assert(runtime.invoke_calls == 0);
    assert(sequence.GetSnapshot().phase == WakePhase::kDrainingOutput);

    sequence.Poll(92'100 + maomi::kWakeOutputDrainMs - 1, kDeviceStateIdle, true);
    assert(runtime.invoke_calls == 0);

    sequence.Poll(92'100 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);
    assert(runtime.invoke_calls == 1);
    assert(sequence.GetSnapshot().phase == WakePhase::kAwaitingOfficial);
}

void TestConnectingWithoutProgressTimesOutAndRestoresDetection() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());

    sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, 95'000);
    sequence.HandlePlaybackFinished(95'100, kDeviceStateIdle, runtime.playback_id);
    sequence.Poll(95'100 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);
    sequence.Poll(95'100 + maomi::kWakeOutputDrainMs + maomi::kWakeOfficialStartTimeoutMs - 1,
                  kDeviceStateConnecting, true);
    assert(sequence.IsBusy());

    sequence.Poll(95'100 + maomi::kWakeOutputDrainMs + maomi::kWakeOfficialStartTimeoutMs,
                  kDeviceStateConnecting, true);
    assert(!sequence.IsBusy());
    assert(runtime.abort_official_calls == 1);
    assert(runtime.restore_calls == 1);
    assert(sequence.GetSnapshot().recovery_count == 1);
}

void TestHundredSequencesLeaveNoPendingWork() {
    FakeRuntime runtime;
    WakeSequence sequence(runtime.Dependencies());
    WakePollGate poll_gate;
    uint64_t now_ms = 100'000;

    for (int i = 0; i < 100; ++i) {
        assert(sequence.HandleWakeWord("猫咪过来", kDeviceStateIdle, now_ms) ==
               WakeHandleResult::kStarted);
        sequence.HandlePlaybackFinished(now_ms + 100, kDeviceStateIdle, runtime.playback_id);
        sequence.Poll(now_ms + 100 + maomi::kWakeOutputDrainMs, kDeviceStateIdle, true);
        sequence.Poll(now_ms + 200, kDeviceStateConnecting, true);
        assert(poll_gate.TryAcquire());
        assert(!poll_gate.TryAcquire());
        sequence.Poll(now_ms + 300, kDeviceStateListening, true);
        poll_gate.Release();
        assert(!sequence.IsBusy());
        assert(!poll_gate.IsPending());
        assert(sequence.GetSnapshot().phase == WakePhase::kIdle);
        now_ms += maomi::kWakeCooldownMs;
    }

    const auto snapshot = sequence.GetSnapshot();
    assert(snapshot.started_count == 100);
    assert(snapshot.official_invoke_count == 100);
    assert(snapshot.pending_operations == 0);
    assert(runtime.stop_upload_calls == 100);
    assert(runtime.play_calls == 100);
    assert(runtime.invoke_calls == 100);
    assert(runtime.cancel_calls == 0);
}

}  // namespace

int main() {
    TestLocalResponsePrecedesOfficialAndStopsUpload();
    TestOfflineResponseRestoresDetection();
    TestMissingCustomSoundUsesFallbackOnce();
    TestUnrelatedPlaybackDrainDoesNotStartOfficialFlow();
    TestWrongPlaybackTicketIsIgnored();
    TestPlaybackFailureRecoversWithoutListening();
    TestConnectionFailureBackAtIdleRestoresDetection();
    TestIllegalStatesPassThroughUntouched();
    TestTwoSecondCooldownSuppressesEchoAndRestoresDetection();
    TestStalledPlaybackTimesOutAndClearsWork();
    TestMaximumAcceptedSoundHasTimeoutMargin();
    TestPlaybackIdleBeforeCompletionCallbackDoesNotWinRace();
    TestHardwareOutputDrainPrecedesOfficialInvoke();
    TestConnectingWithoutProgressTimesOutAndRestoresDetection();
    TestHundredSequencesLeaveNoPendingWork();
    std::cout << "maomi_wake tests passed" << std::endl;
    return 0;
}
