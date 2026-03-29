#!/usr/bin/env python3
"""
Optuna-based parameter tuner for Enigma chess engine.

Uses Bayesian optimization to find better UCI parameter values by playing
matches against the default configuration.
"""

import argparse
import logging
import random
import sys
import time
import traceback
import warnings
from dataclasses import dataclass
from multiprocessing import Process, Queue

import chess
import chess.engine
import chess.pgn
import optuna
from optuna.exceptions import ExperimentalWarning

from lib.path import BINARY_PATH, OPENINGS_PATH
from lib.concurrency import calc_concurrency

# --- Parameter definitions ---


@dataclass
class Param:
    name: str
    default: float
    min_val: float
    max_val: float
    is_int: bool


PARAMS = [
    # Search
    Param("AspirationWindow", 25, 1, 200, True),
    Param("ScoreDropThreshold", 50, 1, 500, True),
    Param("NullMoveBaseReduction", 2, 1, 4, True),
    Param("NullMoveDeeperThreshold", 6, 2, 12, True),
    Param("NullMoveMinDepth", 3, 1, 6, True),
    Param("FutilityMarginPerDepth", 90, 10, 300, True),
    Param("FutilityMarginBase", 40, 0, 200, True),
    Param("FutilityMaxDepth", 4, 1, 8, True),
    Param("SEECutoff", -200, -500, 0, True),
    Param("LMRTuningConstant", 2.0, 0.5, 5.0, False),
    Param("MinimumIIDDepth", 4, 1, 8, True),
    Param("IIDDepthDivisor", 2, 2, 4, True),
    Param("LMRPVReduction", 1, 0, 3, True),
    Param("BestMoveMinStability", 0, 0, 5, True),
    Param("NullMoveDeepReduction", 5, 1, 8, True),
    Param("HistoryMalusDivisor", 2, 1, 8, True),
    # Time management
    Param("MovesLeftBase", 10, 1, 50, True),
    Param("MovesLeftPhaseScale", 30, 1, 100, True),
    Param("MinMovesNoIncrement", 45, 10, 100, True),
    Param("IncrementFraction", 0.5, 0.1, 1.0, False),
    Param("SoftFactorNoIncrement", 0.5, 0.1, 1.0, False),
    Param("SoftFactorIncrement", 0.6, 0.1, 1.0, False),
    Param("HardFactor", 2.625, 1.0, 5.0, False),
    Param("HardCapDivisor", 3, 1, 10, True),
    Param("EmergencyTrigger", 4, 1, 20, True),
    Param("EmergencySoftDivisor", 15, 2, 50, True),
    Param("EmergencyHardDivisor", 8, 2, 30, True),
]

# --- Tuning config ---

PARAM_PATIENCE = 50
WARMUP_TRIALS = 10
FLOAT_TOLERANCE = 0.01
GAMES_PER_TRIAL = 1200
NUM_OPENINGS = 500

# Scale real-world time controls down uniformly so the clock/increment ratio stays intact.
# The factor is chosen so 5+0 becomes 1+0 for tuning.
TC_SCALE = 1 / 300


def _format_tc_value(seconds):
    return f"{seconds:.6f}".rstrip("0").rstrip(".")


def _scaled_tc(minutes, increment_seconds):
    base_seconds = minutes * 60 * TC_SCALE
    increment_scaled = increment_seconds * TC_SCALE
    return f"{_format_tc_value(base_seconds)}+{_format_tc_value(increment_scaled)}"


TIME_CONTROLS = [
    _scaled_tc(1, 0),   # 1+0 bullet
    _scaled_tc(2, 1),   # 2+1 bullet
    _scaled_tc(3, 0),   # 3+0 blitz
    _scaled_tc(3, 2),   # 3+2 blitz
    _scaled_tc(5, 0),   # 5+0 blitz
    _scaled_tc(5, 3),   # 5+3 blitz
]

# --- Openings ---


def _load_openings(path):
    """Load opening positions from a PGN file, returning a list of UCI move strings."""
    openings = []
    with open(path) as f:
        while len(openings) < NUM_OPENINGS:
            game = chess.pgn.read_game(f)
            if game is None:
                break

            moves = []
            board = game.board()
            for move in game.mainline_moves():
                moves.append(move.uci())
                board.push(move)
            
            if moves:
                openings.append(moves)

    return openings


# --- Engine helpers ---


def _configure_engine(params):
    """Start a UCI engine and configure it with the given tunable parameters."""
    engine = chess.engine.SimpleEngine.popen_uci(BINARY_PATH)
    engine.configure({"OwnBook": False, **params})
    return engine


def _play_game(engine_a, engine_b, opening_moves, tc):
    """Play a single game. Returns 1.0/0.5/0.0 from engine A's perspective (A is white)."""
    board = chess.Board()
    for uci_move in opening_moves:
        board.push(chess.Move.from_uci(uci_move))

    # Parse time control string (e.g. "1+0.01" -> 1s base, 0.01s increment)
    engines = [engine_a, engine_b]
    parse_tc = tc.split("+")
    base_sec = float(parse_tc[0])
    inc_sec = float(parse_tc[1]) if len(parse_tc) > 1 else 0.0
    times = [base_sec, base_sec]

    while not board.is_game_over(claim_draw=True):
        side = 0 if board.turn == chess.WHITE else 1
        limit = chess.engine.Limit(
            white_clock=times[0],
            black_clock=times[1],
            white_inc=inc_sec,
            black_inc=inc_sec,
        )

        # Track elapsed time to update the clock
        start = time.monotonic()
        result = engines[side].play(board, limit)
        elapsed = time.monotonic() - start

        if result.move is None:
            break
        board.push(result.move)
        times[side] = max(0.0, times[side] - elapsed + inc_sec)

    result = board.result(claim_draw=True)
    if result == "1-0":
        return 1.0
    elif result == "0-1":
        return 0.0
    else:
        return 0.5


def _match_worker(params_trial, params_default, work_queue, result_queue):
    """Worker process: pulls (opening, tc) pairs and plays both color orderings."""
    engine_trial = _configure_engine(params_trial)
    engine_default = _configure_engine(params_default)

    try:
        while True:
            try:
                opening, tc = work_queue.get_nowait()
            except Exception:
                break

            # Play both color orderings to cancel out first-move advantage
            score = _play_game(engine_trial, engine_default, opening, tc)
            result_queue.put(score)

            score = _play_game(engine_default, engine_trial, opening, tc)
            result_queue.put(1.0 - score)  # Flip so score is always from trial's perspective

    except Exception as exc:
        result_queue.put({
            "type": "crash",
            "error": repr(exc),
            "traceback": traceback.format_exc(),
        })
    finally:
        for engine in (engine_trial, engine_default):
            try:
                engine.quit()
            except chess.engine.EngineTerminatedError:
                pass


def _play_match(params_trial, params_default, openings, games_per_trial):
    """Play a match across all TCs. Returns win rate for trial params."""
    concurrency = calc_concurrency(threads=1)

    # Divide total games evenly across TCs, then across openings (2 games per opening: both colors)
    games_per_tc = games_per_trial // len(TIME_CONTROLS)
    work_items = []
    for tc in TIME_CONTROLS:
        num_openings = games_per_tc // 2
        selected = random.sample(openings, min(num_openings, len(openings)))
        for opening in selected:
            work_items.append((opening, tc))
    random.shuffle(work_items)

    # Distribute work across parallel workers
    work_queue = Queue()
    for item in work_items:
        work_queue.put(item)

    result_queue = Queue()
    workers = []
    for _ in range(concurrency):
        p = Process(target=_match_worker, args=(params_trial, params_default, work_queue, result_queue))
        p.start()
        workers.append(p)

    # Collect results (2 games per work item: one per color)
    total_games = len(work_items) * 2
    total_score = 0.0
    games_played = 0

    for _ in range(total_games):
        score = result_queue.get()

        if isinstance(score, dict) and score.get("type") == "crash":
            for p in workers:
                if p.is_alive():
                    p.terminate()
            for p in workers:
                p.join()
            raise RuntimeError(
                f"Worker crashed: {score['error']}\n{score['traceback']}"
            )

        total_score += score
        games_played += 1

    for p in workers:
        if p.is_alive():
            p.terminate()
    for p in workers:
        p.join()

    return total_score / games_played if games_played > 0 else 0.5


# --- Display ---

# Number of lines in the display block: header + params + blank + status
NUM_DISPLAY_LINES = len(PARAMS) + 3


def _format_params(best_params, frozen_params, param_last_changed, trial_num):
    """Format best parameter values with per-param patience or frozen status."""
    name_w = max(len(p.name) for p in PARAMS)
    lines = []

    for p in PARAMS:
        val = best_params.get(p.name, p.default)

        if p.is_int:
            val = round(val)
            default = round(p.default)
            diff = val - default
            default_str = f"{default:>7d}"
            val_str = f"{val:>7d}"
            diff_str = f"{diff:+d}"
        else:
            default = p.default
            diff = val - default
            default_str = f"{default:>7.3f}"
            val_str = f"{val:>7.3f}"
            diff_str = f"{diff:+.3f}"

        if p.name in frozen_params:
            frozen_at = frozen_params[p.name][1]
            status = f"  frozen @{frozen_at}"
        else:
            pct = diff / abs(default) * 100 if default != 0 else 0
            stale = trial_num - param_last_changed.get(p.name, 0)
            remaining = max(0, PARAM_PATIENCE - stale)
            status = f"  ({diff_str:>7s}, {pct:>+5.0f}%)  {remaining:>2d}/{PARAM_PATIENCE}"

        lines.append(f"  {p.name:<{name_w}}  {default_str} --> {val_str}{status}")

    return lines


def _render_display(best_params, frozen_params, param_last_changed, trial_num, best_trial_num=None, score="", first=False):
    """Render the display block, overwriting previous output unless first=True."""

    # Move cursor up to overwrite the previous display
    if not first:
        sys.stdout.write(f"\033[{NUM_DISPLAY_LINES - 1}F")

    # Clear each line before writing to avoid leftover characters
    cl = "\033[2K"
    best_str = f" (trial {best_trial_num})" if best_trial_num is not None else ""
    active = sum(1 for p in PARAMS if p.name not in frozen_params)
    sys.stdout.write(f"{cl}trial {trial_num}  |  best score {score}{best_str}  |  {active}/{len(PARAMS)} active\n")

    for line in _format_params(best_params, frozen_params, param_last_changed, trial_num):
        sys.stdout.write(f"{cl}{line}\n")

    sys.stdout.write(f"{cl}")
    sys.stdout.flush()


# --- Per-param freezing ---


def _param_values_match(a, b, is_int):
    """Check if two param values are equal (exact for ints, tolerance for floats)."""
    if is_int:
        return round(a) == round(b)
    return abs(a - b) < FLOAT_TOLERANCE


# --- Optimizer ---


def main():
    parser = argparse.ArgumentParser(description="Tune Enigma engine parameters")
    parser.add_argument("--games", type=int, default=GAMES_PER_TRIAL, help="games per trial")
    args = parser.parse_args()

    # Suppress Optuna's default logging so we can show our own display
    optuna.logging.set_verbosity(optuna.logging.WARNING)

    openings = _load_openings(OPENINGS_PATH)
    default_params = {p.name: (round(p.default) if p.is_int else p.default) for p in PARAMS}

    # Per-param freezing state
    frozen_params = {}           # name -> (value, trial_num_frozen)
    param_last_changed = {}      # name -> trial_num when best value last changed
    prev_best_params = None

    def suggest_params(trial):
        params = {}
        for p in PARAMS:
            if p.name in frozen_params:
                params[p.name] = frozen_params[p.name][0]
            elif p.is_int:
                params[p.name] = trial.suggest_int(p.name, int(p.min_val), int(p.max_val))
            else:
                params[p.name] = trial.suggest_float(p.name, p.min_val, p.max_val)
        return params

    def objective(trial):
        trial_params = suggest_params(trial)
        return _play_match(trial_params, default_params, openings, args.games)

    def after_trial(study, trial):
        nonlocal prev_best_params

        best = study.best_trial
        trial_num = trial.number + 1
        best_num = best.number + 1
        best_params = best.params

        # Update per-param last-changed timestamps
        if prev_best_params is not None:
            for p in PARAMS:
                if p.name in frozen_params:
                    continue
                prev_val = prev_best_params.get(p.name, p.default)
                curr_val = best_params.get(p.name, p.default)
                if not _param_values_match(prev_val, curr_val, p.is_int):
                    param_last_changed[p.name] = trial_num

        prev_best_params = dict(best_params)

        # Freeze params that haven't moved since warmup
        if trial_num >= WARMUP_TRIALS:
            for p in PARAMS:
                if p.name in frozen_params:
                    continue
                last_changed = param_last_changed.get(p.name, 0)
                if trial_num - last_changed >= PARAM_PATIENCE:
                    val = best_params.get(p.name, p.default)
                    if p.is_int:
                        val = round(val)
                    frozen_params[p.name] = (val, trial_num)

        _render_display(
            best_params, frozen_params, param_last_changed, trial_num,
            best_trial_num=best_num, score=f"{best.value:.3f}",
        )

        if len(frozen_params) == len(PARAMS):
            study.stop()

    warnings.filterwarnings("ignore", category=ExperimentalWarning)
    sampler = optuna.samplers.TPESampler(multivariate=True, group=True)
    study = optuna.create_study(direction="maximize", sampler=sampler, study_name="enigma-param-tune")

    _render_display(default_params, frozen_params, param_last_changed, 0, score="—", first=True)

    try:
        study.optimize(objective, callbacks=[after_trial])
    except KeyboardInterrupt:
        pass

    print("\n")
    if study.trials:
        best = study.best_trial
        print(f"best trial: {best.number + 1}  |  score: {best.value:.3f}")
        for p in PARAMS:
            val = best.params.get(p.name, p.default)
            if p.is_int:
                val = round(val)
            frozen_tag = ""
            if p.name in frozen_params:
                frozen_tag = f"  (frozen @{frozen_params[p.name][1]})"
            print(f"  {p.name}: {val}{frozen_tag}")


if __name__ == "__main__":
    main()
