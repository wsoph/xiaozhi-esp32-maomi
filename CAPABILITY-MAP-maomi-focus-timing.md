# Capability Map: Maomi Focus Timing

| Module id | Responsibility | Depends on |
|---|---|---|
| `timer-session-core` | Own one foreground timing session and its running, paused, resumed, stopped, and reset transitions | — |
| `wake-to-listen` | After the local cat sound, enter listening directly without a greeting response or pet expression | — |
| `countdown-focus` | Put an active countdown into focus standby and expose voice controls through the timer session contract | `timer-session-core`, `wake-to-listen` |
| `stopwatch` | Count upward on the focus display and expose the same pause, resume, stop, and reset controls | `timer-session-core`, `wake-to-listen` |

Build order: `timer-session-core` and `wake-to-listen` → `countdown-focus` → `stopwatch`.

The four module ids are stable for the lifetime of this initiative. Alarm, water, sedentary,
pomodoro, bonding, and unrelated pet behavior remain outside its scope.
