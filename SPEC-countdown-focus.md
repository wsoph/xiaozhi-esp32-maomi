# Spec: Countdown Focus

## Objective

Turn the existing countdown into a focus session. Once creation succeeds, the current voice turn
ends, the full-screen remaining time stays visible, normal speech is ignored, and local wake-word
detection remains available for an intentional command such as pause, resume, or stop.

## Tech Stack

- Existing `ReminderEngine` countdown and monotonic deadline behavior.
- `timer-session-core` foreground state contract.
- Existing Maomi full-screen LVGL countdown layer.
- Existing MCP server, audio service, and application state machine.

## Commands

- Reminder host tests: `python -m unittest scripts.tests.test_maomi_host_cpp -v`
- MCP contract tests: `python -m unittest scripts.tests.test_maomi_tools -v`
- Full host tests: `python -m unittest discover -s scripts/tests -v`
- Firmware build: `python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi`
- Format touched C/C++: `clang-format -i <touched-files>`

## Project Structure

- `maomi_reminders.*`: countdown pause/resume and remaining-time semantics.
- `maomi_tools.*`: start and `self.timer.control` MCP contracts.
- `maomi_lcd_display.h`: running/paused full-screen presentation.
- Maomi board source: voice-session termination, standby, wake, and display wiring.
- `scripts/tests/`: engine, MCP, display, and board integration tests.

## Code Style

```cpp
TimerControlResult PauseForegroundTimer(uint64_t monotonic_ms);
```

Return explicit statuses; never report success until the engine state changed. Keep display output a
projection of engine state rather than a second timer.

## Testing Strategy

- RED tests first for frozen remaining time, resume deadline adjustment, completion after resume,
  rejected duplicate start, and control with no active session.
- Contract-test `self.timer.control` actions `pause`, `resume`, and `stop`.
- Board tests verify successful start exits listening/speaking, preserves wake detection, suppresses
  autonomous meows, and restores ordinary behavior after completion or stop.
- Display tests verify a paused countdown keeps large digits and shows a graphical pause mark.

## Boundaries

- Always: retain 1–86400 second validation and countdown completion sound/animation.
- Ask first: changing alarm/interval/pomodoro behavior or making countdown persistent.
- Never: upload ambient speech while in focus standby, hide the remaining time behind chat text, or
  discard a valid timer because voice-session shutdown fails.

## Success Criteria

- Successful countdown creation leaves the device idle except for wake-word detection and timer
  polling; current listening/speaking is terminated without an extra assistant utterance.
- Remaining time continues to update once per second while running.
- “Pause countdown” freezes time and shows a pause mark; “resume countdown” continues from the exact
  frozen duration; “stop countdown” removes it.
- Wake commands use `wake-to-listen`; after the command turn the device returns to focus standby if
  the countdown still exists.
- At zero, the existing reminder animation and prompt play, then focus standby ends.

## Open Questions

None. Explicit wake-word or BOOT-button activation remains allowed; ambient speech is ignored.
