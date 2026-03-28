#!/usr/bin/env python3
"""
Optuna-based parameter tuner for Enigma chess engine.

Uses Bayesian optimization to find better UCI parameter values by playing
matches against the default configuration.
"""

import logging
import random
import sys
import time
import traceback
from dataclasses import dataclass
from multiprocessing import Process, Queue

import chess
import chess.engine
import chess.pgn
import optuna

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

PATIENCE = 30
GAMES_PER_TRIAL = 500
NUM_OPENINGS = 500
TCS = [
    "0.5+0",       # 1+0
    "0.5+0.25",    # 2+1
    "0.75+0",      # 3+0
    "0.75+0.5",    # 3+2
    "1+0",         # 5+0
    "1+0.6",       # 5+3
]

# --- Openings ---


def load_openings(path):
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


def configure_engine(params):
    """Start a UCI engine and configure it with the given tunable parameters."""
    engine = chess.engine.SimpleEngine.popen_uci(BINARY_PATH)
    engine.configure({"OwnBook": False, **params})
    return engine


def play_game(engine_a, engine_b, opening_moves, tc):
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


def match_worker(params_trial, params_default, work_queue, result_queue):
    """Worker process: pulls (opening, tc) pairs and plays both color orderings."""
    engine_trial = configure_engine(params_trial)
    engine_default = configure_engine(params_default)

    try:
        while True:
            try:
                opening, tc = work_queue.get_nowait()
            except Exception:
                break

            # Play both color orderings to cancel out first-move advantage
            score = play_game(engine_trial, engine_default, opening, tc)
            result_queue.put(score)

            score = play_game(engine_default, engine_trial, opening, tc)
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


def play_match(params_trial, params_default, openings):
    """Play a match across all TCs. Returns win rate for trial params."""
    concurrency = calc_concurrency(threads=1)

    # Divide total games evenly across TCs, then across openings (2 games per opening: both colors)
    games_per_tc = GAMES_PER_TRIAL // len(TCS)
    work_items = []
    for tc in TCS:
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
        p = Process(target=match_worker, args=(params_trial, params_default, work_queue, result_queue))
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


def format_params(best_params):
    """Format best parameter values as: name  default --> new  (diff, pct%)"""
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

        pct = diff / abs(default) * 100 if default != 0 else 0
        change_col = f"  ({diff_str:>7s}, {pct:>+5.0f}%)"

        lines.append(f"  {p.name:<{name_w}}  {default_str} --> {val_str}{change_col}")

    return lines


def render_display(best_params, trial_num, best_trial_num=None, score="", first=False):
    """Render the display block, overwriting previous output unless first=True."""

    # Move cursor up to overwrite the previous display
    if not first:
        sys.stdout.write(f"\033[{NUM_DISPLAY_LINES - 1}F")

    # Clear each line before writing to avoid leftover characters
    cl = "\033[2K"
    best_str = f" (trial {best_trial_num})" if best_trial_num is not None else ""
    stale = trial_num - best_trial_num if best_trial_num is not None else 0
    sys.stdout.write(f"{cl}trial {trial_num}  |  best score {score}{best_str}  |  patience {PATIENCE - stale}/{PATIENCE}\n")

    for line in format_params(best_params):
        sys.stdout.write(f"{cl}{line}\n")

    sys.stdout.write(f"{cl}")
    sys.stdout.flush()


# --- Optimizer ---


def suggest_params(trial):
    """Use Optuna to suggest values for all tunable parameters."""
    params = {}
    for p in PARAMS:
        if p.is_int:
            params[p.name] = trial.suggest_int(p.name, int(p.min_val), int(p.max_val))
        else:
            params[p.name] = trial.suggest_float(p.name, p.min_val, p.max_val)
    return params


def main():
    # Suppress Optuna's default logging so we can show our own display
    optuna.logging.set_verbosity(optuna.logging.WARNING)

    openings = load_openings(OPENINGS_PATH)
    default_params = {p.name: (round(p.default) if p.is_int else p.default) for p in PARAMS}

    def objective(trial):
        trial_params = suggest_params(trial)
        return play_match(trial_params, default_params, openings)

    # Callback to redraw the display and stop if patience is exhausted
    def after_trial(study, trial):
        best = study.best_trial
        trial_num = trial.number + 1
        best_num = best.number + 1
        render_display(
            best.params, trial_num,
            best_trial_num=best_num, score=f"{best.value:.3f}",
        )

        # Stop if no improvement in PATIENCE trials
        if trial_num - best_num >= PATIENCE:
            study.stop()

    sampler = optuna.samplers.TPESampler(multivariate=True, group=True)
    study = optuna.create_study(direction="maximize", sampler=sampler, study_name="enigma-param-tune")

    # Show defaults before the first trial starts
    render_display(default_params, 0, score="—", first=True)
    study.optimize(objective, callbacks=[after_trial])

    print()


if __name__ == "__main__":
    main()
