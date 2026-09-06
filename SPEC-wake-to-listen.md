# Spec: Wake To Listen

## Objective

For the Maomi board, every accepted local wake follows one sequence: play the local cat sound once,
wait until that exact playback finishes, then enter listening immediately. Do not ask the server to
respond to the wake phrase and do not display a wake-triggered pet interaction.

## Tech Stack

- Existing `maomi::WakeSequence`, playback ticket, timeout, and recovery policy.
- Existing `Application` device state machine and transport-neutral `Protocol::SendStartListening()`.
- Existing custom wake model and `maomi_wake.ogg` asset.

## Commands

- Wake host tests: `python -m unittest scripts.tests.test_maomi_host_cpp -v`
- Audio contract tests: `python -m unittest scripts.tests.test_maomi_audio_contract -v`
- Full host tests: `python -m unittest discover -s scripts/tests -v`
- Firmware build: `python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi`
- Format touched C/C++: `clang-format -i <touched-files>`

## Project Structure

- `main/boards/zhengchen/1.54tft-wifi-maomi/maomi_wake.*`: local playback sequencing.
- `main/boards/zhengchen/1.54tft-wifi-maomi/zhengchen-1.54tft-wifi-maomi.cc`: board wiring.
- `main/application.*`: transport-neutral direct-listening entry point if needed.
- `scripts/tests/maomi_wake_test.cc` and audio contract tests: behavioral coverage.

## Code Style

```cpp
if (playback_id != expected_playback_id_) {
    return;
}
```

Keep playback completion ticketed, main-task-owned, nonblocking, and explicit about recovery.

## Testing Strategy

- Prove the old server wake-invocation path is not used after successful local playback.
- Prove direct listening starts only after the matching playback ticket and output drain.
- Prove no `Event::UserWake()` expression is submitted while autonomy activity still wakes the
  display and resets sleep timing.
- Retain failure, timeout, duplicate-wake, and illegal-state recovery tests.

## Boundaries

- Always: preserve the local cat sound, the two-second duplicate guard, and failure recovery.
- Ask first: changing the wake word/model or changing wake behavior for other boards.
- Never: send buffered wake-word audio as the user's first request on this Maomi path, play an
  additional greeting, or bypass `Application::SetDeviceState()`.

## Success Criteria

- The cat sound plays exactly once for an accepted idle wake.
- Its completion leads directly to the configured listening mode with microphone upload enabled.
- No server-generated greeting or popup sound precedes listening.
- The display uses the listening presentation, not curious, petting, feeding, or playing animation.
- Failures return to idle with wake detection restored.

## Open Questions

None. Existing wake-related relationship accounting is preserved where it does not require a visual
expression.
