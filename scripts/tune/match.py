"""
Shared match infrastructure for parameter tuning.

Provides engine configuration, game play, and parallel match execution
used by the parameter tuner.
"""

import random
import signal
import time
import traceback
from multiprocessing import Process, Queue

import chess
import chess.engine
import chess.pgn

from lib.path import BINARY_PATH, OPENINGS_PATH
from lib.concurrency import calc_concurrency

TIME_CONTROLS = [
    '8+0', # no increment
    '8+0.08', # with increment
]

MATCH_SEED = 42
MOVE_OVERHEAD_SEC = 0.01


class MatchConfig:
    """Opaque match state: openings + pre-sampled work items."""

    def __init__(self, games_per_tc):
        num_openings = games_per_tc // 2
        openings = _load_openings(num_openings)
        rng = random.Random(MATCH_SEED)

        # Each TC gets games_per_tc games, split across openings (2 games per opening: both colors)
        self.work_items = []
        for tc in TIME_CONTROLS:
            selected = rng.sample(openings, min(num_openings, len(openings)))
            for opening in selected:
                self.work_items.append((opening, tc))

        rng.shuffle(self.work_items)


def _load_openings(count, path=OPENINGS_PATH):
    """Load opening positions from a PGN file, returning a list of UCI move strings."""
    openings = []
    with open(path) as f:
        while len(openings) < count:
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


def _configure_engine(params):
    """Start a UCI engine and configure it with the given tunable parameters."""
    engine = chess.engine.SimpleEngine.popen_uci(BINARY_PATH)
    engine.configure({'OwnBook': False, **params})
    return engine


def _play_game(engine_a, engine_b, opening_moves, tc):
    """Play a single game. Returns 1.0/0.5/0.0 from engine A's perspective (A is white)."""
    board = chess.Board()
    for uci_move in opening_moves:
        board.push(chess.Move.from_uci(uci_move))

    # Parse time control string (e.g. "1+0.01" -> 1s base, 0.01s increment)
    engines = [engine_a, engine_b]
    games = [object(), object()]
    parse_tc = tc.split('+')
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
        result = engines[side].play(board, limit, game=games[side])
        elapsed = time.monotonic() - start

        if result.move is None:
            break

        # Forfeit if the move took longer than remaining time (with overhead)
        if elapsed + MOVE_OVERHEAD_SEC > times[side]:
            return 0.0 if side == 0 else 1.0

        board.push(result.move)
        times[side] = times[side] - elapsed - MOVE_OVERHEAD_SEC + inc_sec

    result = board.result(claim_draw=True)
    if result == '1-0':
        return 1.0
    elif result == '0-1':
        return 0.0
    else:
        return 0.5


def _match_worker(params_trial, params_default, work_queue, result_queue):
    """Worker process: pulls (opening, tc) pairs and plays both color orderings."""
    # Ignore SIGINT in workers — the parent handles Ctrl+C and terminates us
    signal.signal(signal.SIGINT, signal.SIG_IGN)

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
            'type': 'crash',
            'error': repr(exc),
            'traceback': traceback.format_exc(),
        })
    finally:
        for engine in (engine_trial, engine_default):
            try:
                engine.quit()
            except chess.engine.EngineTerminatedError:
                pass


def _kill_workers(workers):
    """Terminate and join all worker processes."""
    for p in workers:
        if p.is_alive():
            p.terminate()
    for p in workers:
        p.join()


def play_match(params_trial, params_default, match_config):
    """Play a match across all TCs. Returns win rate for trial params."""
    concurrency = calc_concurrency(threads=1)

    # Distribute work across parallel workers
    work_queue = Queue()
    for item in match_config.work_items:
        work_queue.put(item)

    result_queue = Queue()
    workers = []
    for _ in range(concurrency):
        p = Process(target=_match_worker, args=(params_trial, params_default, work_queue, result_queue))
        p.start()
        workers.append(p)

    # Collect results (2 games per work item: one per color)
    total_games = len(match_config.work_items) * 2
    total_score = 0.0
    games_played = 0

    try:
        for _ in range(total_games):
            score = result_queue.get()

            if isinstance(score, dict) and score.get('type') == 'crash':
                _kill_workers(workers)
                raise RuntimeError(
                    f"Worker crashed: {score['error']}\n{score['traceback']}"
                )

            total_score += score
            games_played += 1
    except KeyboardInterrupt:
        result_queue.cancel_join_thread()
        work_queue.cancel_join_thread()
        _kill_workers(workers)
        raise

    _kill_workers(workers)
    return total_score / games_played if games_played > 0 else 0.5
