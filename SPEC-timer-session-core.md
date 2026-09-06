# Spec: Timer Session Core

## Objective

Provide one deterministic foreground timing session shared by countdown and stopwatch. A session
is either running or paused and can be resumed or stopped. Reset is valid only for a stopwatch.
Alarms, interval reminders, and pomodoro may continue independently in the background.

## Tech Stack

- ESP-IDF C++17 firmware on ESP32-S3.
- Main-task-owned state using the existing `Application::Schedule()` boundary.
- Existing monotonic clock; wall-clock corrections must not change elapsed or remaining time.
- Existing MCP server and fixed-capacity/no-unbounded-allocation conventions.

## Commands

- Focused host tests: `python -m unittest scripts.tests.test_maomi_host_cpp -v`
- MCP contract tests: `python -m unittest scripts.tests.test_maomi_tools -v`
- Full host tests: `python -m unittest discover -s scripts/tests -v`
- Firmware build: `python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi`
- Format touched C/C++: `clang-format -i <touched-files>`
- Check formatting: `clang-format --dry-run -Werror <touched-files>`

## Project Structure

- `main/boards/zhengchen/1.54tft-wifi-maomi/`: timer policy, board integration, display, and MCP wiring.
- `main/application.*`: only an explicit shared listening entry point if required by `wake-to-listen`.
- `scripts/tests/`: deterministic host C++ tests and Python contract tests.
- Root `SPEC-*.md` files: approved behavioral contracts for this initiative.

## Code Style

```cpp
enum class TimingState : uint8_t {
    kRunning,
    kPaused,
};
```

Use scoped enums, fixed-size state, monotonic deadlines, explicit result statuses, and existing
repository formatting. Board callbacks schedule application mutations onto the main task.

## Testing Strategy

- Unit-test every legal and illegal state transition with a fake monotonic clock.
- Contract-test MCP action whitelists and returned state/time fields.
- Board-wiring tests must verify that focus mode suppresses autonomous sound and restores normal
  behavior after stop or completion.
- Build the Maomi board after host tests; physical audio/display behavior still needs hardware.

## Boundaries

- Always: allow at most one foreground countdown or stopwatch; use monotonic time; preserve alarms,
  interval reminders, and pomodoro.
- Ask first: changing reminder persistence schema, adding dependencies, or changing other boards.
- Never: block the application/audio task, make countdown or stopwatch persistent, or edit generated
  `sdkconfig`, build output, or asset headers.

## Success Criteria

- A second foreground timer is rejected with a truthful tool result while one exists.
- Pause freezes the displayed value; elapsed wall time while paused is excluded after resume.
- Stop removes the foreground session and exits focus standby.
- Reset is accepted only for stopwatch and returns it to zero without creating a second session.
- Reboot clears countdown and stopwatch, matching the existing temporary countdown policy.

## Open Questions

None. The user approved a single foreground timing session while background reminders continue.
