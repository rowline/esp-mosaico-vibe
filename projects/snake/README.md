# snake

Classic Snake for the 480×480 ESP-Mosaico touchscreen, built on Raylib Lite.
A 20×18 board with walls; each apple adds one segment and one point, and the
snake speeds up from 6 to about 12.6 cells per second. Hitting a wall or your
own body ends the round. Filling the board wins. The best score lasts for the
session.

## Controls

| Input | Effect |
| --- | --- |
| Swipe (≥ 24 px) | Steer that way; one continuous drag can chain turns |
| Tap on the board | Start; while playing, turn toward the tap (across the current heading) |
| Tap the score bar | Pause; any tap resumes |
| Tap after game over | Play again (accepted 0.5 s after the crash) |
| Simulator keys | Arrows / WASD steer, P pauses, Enter restarts |

Reversing straight into yourself is ignored. Up to three turns queue within one
step, so quick "up, left" works.

## Run

From the workspace root (on this Mac, `source .tools/env.sh` first; see that
file for the local workarounds):

```sh
python mosaico.py game sim --project projects/snake
python3 projects/snake/tests/test_gameplay.py
python mosaico.py game build --project projects/snake
python mosaico.py iris system-update --project projects/snake
```

`test_gameplay.py` drives the shared C model through the engine's Host runner.
It covers eating and growth, wall and body collisions, the reversal guard, the
turn queue, swipes, tap turns, pause, restart and mouse/second-finger pointers.
Each scenario is saved to `build-host/replays/` and replayed through
`mosaico.py game sim --replay`; the final state hash must match.

The simulator page samples left/right/up once per 33 ms tick. A key press
shorter than that can be missed, which only affects scripted key presses.

Device check on 2026-10-03 (board `30:ed:a0:f4:6f:0a`, Vibe Mode 0.1.4): start
screen, tap and swipe confirmed by hand; 33.3 fps with no dropped frames.
`iris screenshot` fails with "screen mirror has an empty frame description"
because this app registers no screen mirror (the BSP games do, via
`raylib_screen_mirror.c`). The cst9220 touch driver logs "Malformed report
header" about every 0.5 s; touch works regardless.

Run `python mosaico.py recover` before first installation on a blank or unverified
device. New applications, changed layouts or external resources use
`iris system-update`. USB High-Speed remains owned by Iris. Touch feel, display
and frame rate still need device validation after Host checks.

## Files

- `main/game.c`: rules, input and state hash (portable C, Host and device).
- `main/game_view.c`: drawing with the Raylib Lite API.
- `main/game_internal.h`: board constants and the instance struct shared by the two.
- `main/game.h`: public interface; one loop owns each instance.
- `main/game_config.h`: identity, display/tick constants and Host action codes.
- `main/game_module.c`: Host lifecycle, key/pointer mapping and JSON state.
- `main/main.c`: device callbacks; touch reaches `game_set_pointer()`.
- `game.sim.json`: sources compiled into the native Host module.
- `tests/test_gameplay.py`: Host gameplay scenarios and replay checks.
