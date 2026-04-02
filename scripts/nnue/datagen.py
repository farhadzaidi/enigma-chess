#!/usr/bin/env python3
"""
NNUE training data generator for the Enigma chess engine.

Spawns parallel workers that play self-play games using the engine, recording
every position's board state, engine evaluation, and game outcome into compact
binary files. Each position is 36 bytes: 32 bytes nibble-encoded pieces,
1 byte side to move, 2 bytes int16 score (white POV), 1 byte outcome.
"""

import argparse
import random
import signal
import struct
import time
import chess
import chess.engine
from multiprocessing import Process, Value
from lib.path import BINARY_PATH
from lib.concurrency import calc_concurrency
from nnue.paths import (
    TRAINING_DATA_DIR, TRAINING_DATA_FILENAME, TRAINING_DATA_GLOB,
    VALIDATION_DATA_DIR, VALIDATION_DATA_FILENAME, VALIDATION_DATA_GLOB,
)

# Number of moves to play randomly in the opening
OPENING_CUTOFF = 10

# Fixed depth for the engine to search to at any given position
SEARCH_DEPTH = 10

# Number of workers to spawn
NUM_WORKERS = calc_concurrency(threads=1)

stopped = Value('b', False)


def _configure_engine():
    """Start a UCI engine process with fixed settings for data generation."""
    engine = chess.engine.SimpleEngine.popen_uci(BINARY_PATH)
    engine.configure({
        'Threads': 1,
        'OwnBook': False,
        'Hash': 256
    })

    return engine


def _prepare_board():
    """Create a board with random opening moves to diversify training positions."""
    board = chess.Board()

    # Play random moves to set up the game
    for _ in range(OPENING_CUTOFF):
        move = random.choice(list(board.legal_moves))
        board.push(move)

        # If the game ends during setup, try again
        if board.is_game_over():
            return _prepare_board()

    return board


def _encode_piece(piece):
    """Return a 4-bit encoding of a piece: MSB is color, 3 LSBs are piece type."""
    if not piece:
        return 0 # empty square

    # The MSB is the color of the piece and the 3 LSB are the piece type
    return (piece.color << 3) | piece.piece_type


def _encode_position(board, score_cp):
    """Pack a board position and score into the 35-byte binary format (outcome added later)."""
    side_to_move = 0 if board.turn else 1 # White = 0, Black = 1
    encoded_side_to_move = struct.pack('<B', side_to_move)
    encoded_score = struct.pack('<h', score_cp)

    # Each square is encoded into a nibble and thus each byte encodes two squares
    encoded_pieces = bytearray(32) # 64 squares / 1 nibble per square = 32 bytes
    for sq in range(64):
        nibble = _encode_piece(board.piece_at(sq))
        byte_index = sq // 2
        if sq % 2 == 0:
            # Even squares encode the least significant nibble
            encoded_pieces[byte_index] |= nibble # 0x0000LLLL
        else:
            # Odd squares encode the most significant nibble
            encoded_pieces[byte_index] |= (nibble << 4) #0xMMMMLLLL

    return encoded_pieces + encoded_side_to_move + encoded_score


def _play_game(engine, positions):
    """Play a self-play game, appending each position's encoded data to the positions list."""
    board = _prepare_board()
    game = object()
    while not board.is_game_over():
        # Have the engine search at a fixed depth and capture the best move and eval
        result = engine.play(
            board,
            chess.engine.Limit(depth=SEARCH_DEPTH),
            info=chess.engine.INFO_SCORE,
            game=game,
        )

        # Discard positions where the search didn't reach the target depth
        if 'score' not in result.info or result.info.get('depth', 0) < SEARCH_DEPTH:
            board.push(result.move)
            continue

        score_cp = result.info['score'].white().score(mate_score=32000) # From white's perspective
        encoded = _encode_position(board, score_cp)
        positions.append(encoded)
        board.push(result.move)

    return board.outcome()


def _write_game_data(positions, outcome, file):
    """Append all positions from a game to disk, each with the backfilled game outcome."""
    # Resolve outcome to a single number
    if outcome.winner is None:
        normalized_outcome = 0 # Draw
    elif outcome.winner:
        normalized_outcome = 1 # White wins
    else:
        normalized_outcome = 2 # Black wins

    for position in positions:
        encoded_outcome = struct.pack('<B', normalized_outcome)
        encoded_position = position + encoded_outcome
        file.write(encoded_position)
    file.flush()


def _worker(worker_id, stopped, data_dir, data_filename):
    """Run a single data generation worker, playing games until stopped."""
    # Ignore SIGINT in child processes; parent will handle keyboard interrupt
    # and coordinate the graceful shutdown of all child processes
    signal.signal(signal.SIGINT, signal.SIG_IGN)

    path = data_dir / data_filename.format(worker_id)
    engine = _configure_engine()
    with open(path, 'ab') as file:
        while not stopped.value:
            positions = []
            outcome = _play_game(engine, positions)
            _write_game_data(positions, outcome, file)
    engine.quit()


def _poll_positions(data_dir, data_glob, label):
    """Print the total position count across all data files (based on file size)."""
    total = 0
    for f in data_dir.glob(data_glob):
        total += f.stat().st_size // 36
    print(f'\r{label} Positions: {total:,}', end='', flush=True)


def _spawn_workers(validation=False):
    """Spawn parallel worker processes and monitor them until interrupted."""
    if validation:
        data_dir = VALIDATION_DATA_DIR
        data_glob = VALIDATION_DATA_GLOB
        data_filename = VALIDATION_DATA_FILENAME
        label = 'Validation'
    else:
        data_dir = TRAINING_DATA_DIR
        data_glob = TRAINING_DATA_GLOB
        data_filename = TRAINING_DATA_FILENAME
        label = 'Training'

    data_dir.mkdir(parents=True, exist_ok=True)

    # Start all processes
    processes = []
    for i in range(NUM_WORKERS):
        p = Process(target=_worker, args=(i, stopped, data_dir, data_filename))
        p.start()
        processes.append(p)

    # Now wait for them to finish (once stop flag is set)
    try:
        while not stopped.value:
            time.sleep(1)
            _poll_positions(data_dir, data_glob, label)
            if any(not p.is_alive() for p in processes):
                stopped.value = True
                for p in processes:
                    p.join()
                raise RuntimeError('Worker died unexpectedly')
    except KeyboardInterrupt:
        print('\nStopping...')
        stopped.value = True
        for p in processes:
            p.join()


def main():
    """Entry point for the data generation script."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--validation', action='store_true', help='Generate validation data')
    args = parser.parse_args()
    _spawn_workers(validation=args.validation)

if __name__ == '__main__':
    main()
