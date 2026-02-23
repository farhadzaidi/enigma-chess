#!/usr/bin/env python3

import math
import argparse
import subprocess
import re

from paths import CUTECHESS_CLI_BINARY_PATH, STOCKFISH_BINARY_PATH, BINARY_PATH, MIXED_EPD_PATH, require_env

require_env('cutechess_cli_binary')
require_env('stockfish_binary')

# Match config
CONCURRENCY = 8
TIMEMARGIN = 50
TIME = 30
INCREMENT = 0
GAMES_PER_BATCH = 150
DRAW_MOVENUMBER = 60
DRAW_MOVECOUNT = 8
DRAW_SCORE = 5
RESIGN_MOVECOUNT = 8
RESIGN_SCORE = 600

# Convergence
MAX_ROUNDS = 6
CONVERGE_THRESHOLD = 25  # Elo difference below which we consider it converged
ELO_SCALE = 400


def score_to_elo_diff(score):
    """Convert a win rate (0-1) to an Elo difference."""
    if score <= 0.0:
        return -ELO_SCALE
    if score >= 1.0:
        return ELO_SCALE
    return -ELO_SCALE * math.log10(1 / score - 1)


def parse_score(output):
    """Parse cutechess-cli output for the final score line. Returns (wins, losses, draws)."""
    matches = re.findall(r'Score of .+: (\d+) - (\d+) - (\d+)', output)
    if not matches:
        print('Error: Could not parse score from cutechess output')
        exit(1)
    # Take the last score line (final result)
    w, l, d = matches[-1]
    return int(w), int(l), int(d)


def run_batch(engine_path, sf_elo, label, use_shuffle=False):
    """Run a batch of games and return (wins, losses, draws)."""
    cmd = [
        str(CUTECHESS_CLI_BINARY_PATH),
        '-engine', f'cmd={engine_path}', 'name=enigma',
        '-engine', f'cmd={STOCKFISH_BINARY_PATH}', 'name=stockfish',
            f'option.UCI_LimitStrength=true', f'option.UCI_Elo={sf_elo}',
        '-each', 'proto=uci', 'ponder=off',
            f'tc={TIME}+{INCREMENT}', f'timemargin={TIMEMARGIN}',
        '-draw', f'movenumber={DRAW_MOVENUMBER}',
            f'movecount={DRAW_MOVECOUNT}', f'score={DRAW_SCORE}',
        '-resign', f'movecount={RESIGN_MOVECOUNT}', f'score={RESIGN_SCORE}',
        '-games', str(GAMES_PER_BATCH),
        '-repeat',
        '-concurrency', str(CONCURRENCY),
    ]

    if use_shuffle:
        cmd.extend(['-openings', f'file={MIXED_EPD_PATH}', 'format=epd', 'order=random'])

    print(f'  Running {label} ({GAMES_PER_BATCH} games)...')
    result = subprocess.run(cmd, capture_output=True, text=True)
    output = result.stdout + result.stderr

    w, l, d = parse_score(output)
    total = w + l + d
    pct = (w + d * 0.5) / total * 100 if total > 0 else 50
    print(f'  {label}: +{w} -{l} ={d} ({pct:.1f}%)')

    return w, l, d


def run_round(engine_path, sf_elo):
    """Run startpos + shuffle batches and return combined Elo difference."""
    print(f'\n  Stockfish Elo: {sf_elo}')
    print(f'  {"-"*40}')

    w1, l1, d1 = run_batch(engine_path, sf_elo, 'Startpos', use_shuffle=False)
    w2, l2, d2 = run_batch(engine_path, sf_elo, 'Shuffle', use_shuffle=True)

    # Combined results
    total_w = w1 + w2
    total_l = l1 + l2
    total_d = d1 + d2
    total = total_w + total_l + total_d
    score = (total_w + total_d * 0.5) / total if total > 0 else 0.5
    elo_diff = score_to_elo_diff(score)

    print(f'  {"-"*40}')
    print(f'  Combined: +{total_w} -{total_l} ={total_d} ({score*100:.1f}%)')
    print(f'  Elo diff: {elo_diff:+.0f}')

    return elo_diff


# Argument parsing
parser = argparse.ArgumentParser(description='Benchmark engine Elo against Stockfish at limited strength')
parser.add_argument('elo_guess', type=int, help='Initial Elo estimate (e.g. 1500)')
args = parser.parse_args()

engine_path = BINARY_PATH.resolve()
current_guess = args.elo_guess

print(f'\n{"="*60}')
print(f'Elo Benchmark')
print(f'Engine: {engine_path}')
print(f'Stockfish: {STOCKFISH_BINARY_PATH}')
print(f'Initial Guess: {current_guess}')
print(f'Games Per Round: {GAMES_PER_BATCH * 2} ({GAMES_PER_BATCH} startpos + {GAMES_PER_BATCH} shuffle)')
print(f'Time Control: {TIME}+{INCREMENT}')
print(f'Max Rounds: {MAX_ROUNDS}')
print(f'Convergence Threshold: {CONVERGE_THRESHOLD} Elo')
print(f'{"="*60}\n')

confirm = input('Confirm (y/N): ')
if confirm.lower() != 'y':
    print('Aborted')
    exit(0)

for round_num in range(1, MAX_ROUNDS + 1):
    # Clamp to Stockfish's UCI_Elo range (1320-3190)
    current_guess = max(1320, min(3190, current_guess))

    print(f'\n--- Round {round_num}/{MAX_ROUNDS} ---')
    elo_diff = run_round(engine_path, current_guess)

    if abs(elo_diff) < CONVERGE_THRESHOLD:
        estimated_elo = current_guess + elo_diff
        print(f'\n{"="*50}')
        print(f'Converged after {round_num} round(s)')
        print(f'Estimated Elo: ~{estimated_elo:.0f}')
        print(f'{"="*50}')
        break

    # Adjust guess based on measured difference
    current_guess = round(current_guess + elo_diff)
    print(f'\n  Adjusting Stockfish Elo to {current_guess} for next round...')

else:
    # Didn't converge within max rounds
    estimated_elo = current_guess + elo_diff
    print(f'\n{"="*50}')
    print(f'Did not fully converge after {MAX_ROUNDS} rounds')
    print(f'Best estimate: ~{estimated_elo:.0f} (last diff: {elo_diff:+.0f})')
    print(f'{"="*50}')
