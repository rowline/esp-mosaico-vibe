#!/usr/bin/env python3
"""Host gameplay checks for Snake.

Drives the shared C model through the engine's Host runner, records every
scenario as a replay, then re-runs each replay through
`mosaico.py game sim --headless --replay` and compares the final state hash.

Run from anywhere: python3 projects/snake/tests/test_gameplay.py
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from collections import deque
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parents[1]
SHIM = WORKSPACE / ".tools/host-cc"
if SHIM.is_dir():
    os.environ["PATH"] = f"{SHIM}{os.pathsep}{os.environ.get('PATH', '')}"
sys.path.insert(0, str(WORKSPACE / "submodule/raylib-lite-engine/host"))
import run_game  # noqa: E402

LEFT, RIGHT, UP, PAUSE, RESET, DOWN = 0, 1, 2, 3, 4, 5
COLS, ROWS = 20, 18
BOARD_X, BOARD_Y, CELL = 20, 64, 22
STEP = {"up": (0, -1), "right": (1, 0), "down": (0, 1), "left": (-1, 0)}
CODE = {"up": UP, "right": RIGHT, "down": DOWN, "left": LEFT}
REVERSE = {"up": "down", "down": "up", "left": "right", "right": "left"}
START_BODY = [(6, 9), (5, 9), (4, 9)]


def cell_center(x: int, y: int) -> tuple[int, int]:
    return BOARD_X + x * CELL + CELL // 2, BOARD_Y + y * CELL + CELL // 2


def attempted_cell(state: dict[str, object]) -> tuple[int, int]:
    dx, dy = STEP[state["dir"]]
    return state["head_x"] + dx, state["head_y"] + dy


def in_bounds(cell: tuple[int, int]) -> bool:
    return 0 <= cell[0] < COLS and 0 <= cell[1] < ROWS


class Sim:
    """Applies input with the same per-frame rules as run_game.run_generic."""

    def __init__(self, runtime: run_game.GenericHostRuntime) -> None:
        self.runtime = runtime
        self.held = {LEFT: False, RIGHT: False, UP: False}
        self.frame = 0
        self.events: list[dict[str, object]] = []

    def state(self) -> dict[str, object]:
        return self.runtime.metadata()

    def tick(self, count: int = 1) -> None:
        for _ in range(count):
            self.runtime.step(self.held[LEFT], self.held[RIGHT], self.held[UP])
            self.frame += 1

    def action(self, code: int, pressed: bool) -> None:
        self.events.append({"frame": self.frame, "type": "action",
                            "code": code, "pressed": pressed})
        if code in self.held:
            self.held[code] = pressed
        else:
            self.runtime.action(code, pressed)

    def press(self, code: int) -> None:
        self.action(code, True)
        self.tick()
        self.action(code, False)

    def pointer(self, x: int, y: int, pressed: bool) -> None:
        self.events.append({"frame": self.frame, "type": "pointer", "track": 0,
                            "x": x, "y": y, "pressed": pressed})
        self.runtime.pointer(0, x, y, pressed)

    def tap(self, x: int, y: int, track: int = 0) -> None:
        self.events.append({"frame": self.frame, "type": "tap", "track": track, "x": x, "y": y})
        self.runtime.pointer(track, x, y, True)
        self.tick()
        self.runtime.pointer(track, x, y, False)

    def wait_for(self, predicate, limit: int = 600) -> dict[str, object]:
        for _ in range(limit):
            state = self.state()
            if predicate(state):
                return state
            self.tick()
        raise AssertionError(f"condition not reached in {limit} frames: {self.state()}")


def reachable(start: tuple[int, int], blocked: set[tuple[int, int]]) -> int:
    seen, queue = {start}, deque([start])
    while queue:
        x, y = queue.popleft()
        for dx, dy in STEP.values():
            cell = (x + dx, y + dy)
            if (0 <= cell[0] < COLS and 0 <= cell[1] < ROWS and cell not in blocked
                    and cell not in seen):
                seen.add(cell)
                queue.append(cell)
    return len(seen)


def grow_to(sim: Sim, target: int, history: list[tuple[int, int]],
            limit: int = 8000) -> None:
    """Greedy food-seeking autopilot; history holds observed body cells, head first."""
    decided = None
    start = sim.frame
    while sim.frame - start < limit:
        state = sim.state()
        assert state["phase"] == "playing", state
        assert state["length"] == 3 + state["score"], state
        head = (state["head_x"], state["head_y"])
        if history[0] != head:
            history.insert(0, head)
        if state["score"] >= target:
            return
        if head == decided:
            sim.tick()
            continue
        decided = head
        body = set(history[:state["length"]])
        food = (state["food_x"], state["food_y"])
        options = []
        for name, (dx, dy) in STEP.items():
            cell = (head[0] + dx, head[1] + dy)
            if name == REVERSE[state["dir"]] or cell in body:
                continue
            if not in_bounds(cell):
                continue
            space = reachable(cell, body)
            distance = abs(cell[0] - food[0]) + abs(cell[1] - food[1])
            options.append((space < state["length"] + 2, distance, name))
        assert options, f"autopilot trapped: {state}"
        choice = min(options)[2]
        if choice != state["dir"]:
            sim.press(CODE[choice])
        else:
            sim.tick()
    raise AssertionError(f"score {target} not reached: {sim.state()}")


def start_by_tap(sim: Sim) -> None:
    sim.tap(240, 300)
    assert sim.state()["phase"] == "playing"


def scenario_eat_and_grow(sim: Sim) -> None:
    state = sim.state()
    assert (state["phase"], state["length"], state["score"]) == ("ready", 3, 0)
    sim.tick(10)
    assert sim.state()["head_x"] == 6, "snake must wait in READY"
    start_by_tap(sim)
    history = list(START_BODY)
    previous_food = None
    for target in range(1, 13):
        grow_to(sim, target, history)
        state = sim.state()
        food = (state["food_x"], state["food_y"])
        assert food != previous_food, "food must respawn after eating"
        assert food not in history[:state["length"]], "food spawned on the snake"
        previous_food = food
    state = sim.state()
    assert state["score"] == 12 and state["length"] == 15, state


def scenario_wall_death(sim: Sim) -> None:
    sim.press(RIGHT)  # A direction key also starts the round.
    state = sim.wait_for(lambda s: s["phase"] != "playing")
    assert state["phase"] == "over" and state["head_x"] == COLS - 1, state
    assert not in_bounds(attempted_cell(state)), state
    tick = state["tick"]
    sim.tick(5)
    state = sim.state()
    assert state["head_x"] == COLS - 1 and state["tick"] == tick + 5, "dead snake moved"


def scenario_reverse_ignored(sim: Sim) -> None:
    start_by_tap(sim)
    sim.press(LEFT)
    sim.wait_for(lambda s: s["head_x"] == 8)
    state = sim.state()
    assert state["dir"] == "right" and state["phase"] == "playing", state


def scenario_turn_queue(sim: Sim) -> None:
    start_by_tap(sim)
    sim.wait_for(lambda s: s["head_x"] == 8)
    sim.press(UP)
    sim.press(LEFT)  # Both inside one step: up first, then left; no reversal death.
    sim.wait_for(lambda s: s["head_x"] == 7)
    state = sim.state()
    assert (state["head_x"], state["head_y"], state["dir"]) == (7, 8, "left"), state
    assert state["phase"] == "playing", state


def scenario_swipe_and_tap_turns(sim: Sim) -> None:
    start_by_tap(sim)
    sim.pointer(240, 300, True)
    sim.tick()
    sim.pointer(240, 340, True)  # 40 px down: a swipe.
    sim.tick()
    sim.pointer(240, 340, False)
    state = sim.wait_for(lambda s: s["head_y"] == 10)
    assert state["dir"] == "down", state
    head_x, head_y = state["head_x"], state["head_y"]
    x, y = cell_center(head_x, head_y)
    sim.tap(x + 3 * CELL, y)  # Tap right of the head while heading down.
    state = sim.wait_for(lambda s: s["head_x"] == head_x + 1)
    assert state["dir"] == "right", state
    x, y = cell_center(state["head_x"], state["head_y"])
    sim.tap(x, y)  # Tap on the head row: no turn.
    sim.tick(12)
    assert sim.state()["dir"] == "right"


def scenario_self_collision(sim: Sim) -> None:
    start_by_tap(sim)
    history = list(START_BODY)
    grow_to(sim, 3, history)  # Length 6 can bite its own body.
    state = sim.state()
    x, y = state["head_x"], state["head_y"]
    back = REVERSE[state["dir"]]
    # Turn sideways (towards whichever side has room), back, then sideways again
    # into the cell just behind the old head.
    if state["dir"] in ("left", "right"):
        side, undo = ("down", "up") if y + 1 < ROWS else ("up", "down")
    else:
        side, undo = ("right", "left") if x + 1 < COLS else ("left", "right")
    for name in (side, back, undo):
        head = (state["head_x"], state["head_y"])
        sim.press(CODE[name])
        state = sim.wait_for(lambda s: (s["head_x"], s["head_y"]) != head
                             or s["phase"] != "playing")
        if state["phase"] != "playing":
            break
    assert state["phase"] == "over", state
    assert in_bounds(attempted_cell(state)), f"expected a body hit, not a wall: {state}"


def scenario_pause_and_restart(sim: Sim) -> None:
    start_by_tap(sim)
    sim.tap(240, 30)  # Score bar pauses.
    state = sim.state()
    assert state["phase"] == "paused", state
    sim.tick(20)
    assert sim.state()["tick"] == state["tick"], "paused game advanced"
    sim.press(DOWN)
    assert sim.state()["dir"] == "right", "steering while paused must be ignored"
    sim.tap(240, 300)  # Any tap resumes.
    assert sim.state()["phase"] == "playing"
    sim.wait_for(lambda s: s["phase"] == "over")
    sim.tap(240, 300)  # Too soon after death: ignored.
    assert sim.state()["phase"] == "over"
    sim.tick(15)
    sim.tap(240, 300)
    state = sim.state()
    assert (state["phase"], state["score"], state["length"]) == ("playing", 0, 3), state
    sim.press(RESET)
    state = sim.state()
    assert (state["phase"], state["tick"], state["length"]) == ("ready", 1, 3), state


def scenario_mouse_pointer(sim: Sim) -> None:
    # The browser simulator reports the mouse as track 1, not 0.
    sim.tap(240, 300, track=1)
    assert sim.state()["phase"] == "playing"
    # A second finger is ignored while the first is down.
    sim.pointer(240, 300, True)
    sim.events.append({"frame": sim.frame, "type": "pointer", "track": 2,
                       "x": 240, "y": 30, "pressed": True})
    sim.runtime.pointer(2, 240, 30, True)
    sim.tick()
    sim.events.append({"frame": sim.frame, "type": "pointer", "track": 2,
                       "x": 240, "y": 30, "pressed": False})
    sim.runtime.pointer(2, 240, 30, False)
    sim.pointer(240, 300, False)
    sim.tick()
    assert sim.state()["phase"] == "playing", "second finger must not pause"


SCENARIOS = [
    scenario_eat_and_grow,
    scenario_wall_death,
    scenario_reverse_ignored,
    scenario_turn_queue,
    scenario_swipe_and_tap_turns,
    scenario_self_collision,
    scenario_pause_and_restart,
    scenario_mouse_pointer,
]


def replay_hash(replay: Path, frames: int, output: Path) -> str:
    subprocess.run(
        [sys.executable, str(WORKSPACE / "mosaico.py"), "game", "sim",
         "--project", str(PROJECT), "--headless", "--frames", str(frames),
         "--replay", str(replay), "--state-output", str(output)],
        check=True, capture_output=True, text=True, env=os.environ)
    return json.loads(output.read_text(encoding="utf-8"))["state_hash"]


def main() -> int:
    replay_dir = PROJECT / "build-host" / "replays"
    replay_dir.mkdir(parents=True, exist_ok=True)
    failures = 0
    with tempfile.TemporaryDirectory(prefix="snake-test-") as directory:
        for index, scenario in enumerate(SCENARIOS):
            runtime = run_game.GenericHostRuntime(PROJECT, Path(directory), index)
            sim = Sim(runtime)
            name = scenario.__name__.removeprefix("scenario_")
            try:
                scenario(sim)
                expected = sim.state()["state_hash"]
                frames = sim.frame
                runtime.close()
                replay = replay_dir / f"{name}.json"
                replay.write_text(json.dumps({"events": sim.events}, indent=1) + "\n",
                                  encoding="utf-8")
                actual = replay_hash(replay, frames, replay_dir / f"{name}.state.json")
                assert actual == expected, f"replay hash {actual} != live {expected}"
                print(f"PASS {name} ({frames} frames, replay {actual})")
            except (AssertionError, subprocess.CalledProcessError) as error:
                failures += 1
                print(f"FAIL {name}: {error}")
    print("OK" if not failures else f"{failures} scenario(s) failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
