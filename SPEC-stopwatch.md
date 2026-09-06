# Spec: Stopwatch

## Objective

Add a nonpersistent count-up timer that uses the same full-screen focus experience and voice control
contract as countdown. It is intended for timing an activity without choosing a duration first.

## Tech Stack

- `timer-session-core` state and monotonic clock policy.
- Existing Maomi LVGL timer surface, extended for count-up and pause indication.
- Existing MCP server and board focus-standby integration.

## Commands

- Focused host tests: `python -m unittest scripts.tests.test_maomi_host_cpp -v`
- MCP contract tests: `python -m unittest scripts.tests.test_maomi_tools -v`
- Full host tests: `python -m unittest discover -s scripts/tests -v`
- Firmware build: `python scripts/build.py zhengchen/1.54tft-wifi-maomi --name zhengchen-1.54tft-wifi-maomi`
- Format touched C/C++: `clang-format -i <touched-files>`

## Project Structure

- Maomi timer policy source: stopwatch state and elapsed-time calculation.
- `maomi_tools.*`: `self.stopwatch.start` plus shared `self.timer.control` wiring.
- `maomi_lcd_display.h`: count-up display using the existing large timer layout.
- Maomi board source: focus standby and periodic display update.
- `scripts/tests/`: deterministic timer, contract, display, and board tests.

## Code Style

```cpp
const uint64_t elapsed_ms = accumulated_ms + (now_ms - started_at_ms);
```

Use saturating arithmetic, explicit state transitions, and no periodic accumulation drift.

## Testing Strategy

- Unit-test start at zero, count-up, pause, resume without paused-time drift, reset, stop, monotonic
  rollback defense, and the display limit.
- Contract-test successful and invalid control operations.
- Board tests verify stopwatch focus standby matches countdown behavior.
- Hardware validation checks digit fit, pause mark visibility, wake latency, and long-running drift.

## Boundaries

- Always: use one foreground session, start at `00:00:00`, and keep background reminders active.
- Ask first: persistence, lap history, multiple stopwatches, or export/history features.
- Never: allow the display value to wrap or allocate an unbounded history.

## Success Criteria

- “Start stopwatch” starts at zero, enters focus standby, and displays increasing `HH:MM:SS`.
- Pause, resume, and stop use `self.timer.control`; reset is accepted only for stopwatch.
- Reset returns the stopwatch to zero and preserves its current running/paused state.
- The display saturates at `99:59:59` without wrapping; the session remains controllable.
- Completion alarms and interval reminders can still interrupt according to existing priority rules.

## Open Questions

None. Stopwatch is temporary and has no lap feature in this scope.
