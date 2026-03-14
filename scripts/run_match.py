#!/usr/bin/env python3
"""
Runs a match between two engine versions using cutechess-cli.
Supports standard fixed-game matches and SPRT testing.
"""

import argparse
import subprocess

import psutil

from lib.path import CUTECHESS_CLI_BINARY_PATH, BINARY_PATH, OPENINGS_PATH, require_env
from lib.version import find_version, find_latest_version

require_env('cutechess_cli_binary')

# Fixed settings
TC = '8+0.08'
TIMEMARGIN = 150
STANDARD_MAX_GAMES = 2000
SPRT_MAX_GAMES = 10000
DRAW_MOVENUMBER = 60
DRAW_MOVECOUNT = 8
DRAW_SCORE = 5
RESIGN_MOVECOUNT = 8
RESIGN_SCORE = 600
SPRT_ALPHA = 0.05
SPRT_BETA = 0.05

PHYSICAL_CORES = psutil.cpu_count(logical=False) or 1
RESERVED_CORES = 2


def calc_concurrency(threads, ponder):
    engines_per_game = 2 if ponder else 1
    usable_cores = max(1, PHYSICAL_CORES - RESERVED_CORES)
    return max(1, usable_cores // (threads * engines_per_game))


def build_cmd(
    engine_a, engine_b, engine_a_name, engine_b_name,
    threads, ponder, concurrency,
    sprt=None, max_games=STANDARD_MAX_GAMES, ordered=False,
):
    ponder_str = 'on' if ponder else 'off'

    cmd = [
        str(CUTECHESS_CLI_BINARY_PATH),
        '-engine', f'cmd={engine_a}', f'name={engine_a_name}',
        '-engine', f'cmd={engine_b}', f'name={engine_b_name}',
        '-each', 'proto=uci', f'ponder={ponder_str}', 'option.OwnBook=false', f'option.Threads={threads}',
        f'tc={TC}', f'timemargin={TIMEMARGIN}',
        '-draw', f'movenumber={DRAW_MOVENUMBER}', f'movecount={DRAW_MOVECOUNT}', f'score={DRAW_SCORE}',
        '-resign', f'movecount={RESIGN_MOVECOUNT}', f'score={RESIGN_SCORE}',
        '-games', str(max_games),
        '-repeat',
        '-concurrency', str(concurrency),
        '-openings', f'file={OPENINGS_PATH}', 'format=pgn',
    ]

    if not ordered:
        cmd.append('order=random')

    if sprt is not None:
        cmd.extend([
            '-sprt',
            f'elo0={sprt["elo0"]}',
            f'elo1={sprt["elo1"]}',
            f'alpha={sprt["alpha"]}',
            f'beta={sprt["beta"]}'
        ])

    return cmd


def print_config(test_type, engine_a_name, engine_b_name, threads, ponder, concurrency, games):
    print()
    print(f'  Test:        {test_type}')
    print(f'  Engine A:    {engine_a_name}')
    print(f'  Engine B:    {engine_b_name}')
    print(f'  TC:          {TC}')
    print(f'  Threads:     {threads}')
    print(f'  Ponder:      {"on" if ponder else "off"}')
    print(f'  Concurrency: {concurrency}')
    print(f'  Games:       {games}')
    print()


parser = argparse.ArgumentParser(
    description='Run match between engine versions using cutechess-cli'
)

parser.add_argument('engine_a_version', nargs='?', help='Engine A version (e.g. v1) - optional')
parser.add_argument('engine_b_version', nargs='?', help='Engine B version (e.g. v2) - optional')
parser.add_argument('--sprt', type=int, metavar='ELO', help='Run SPRT test with expected Elo gain (e.g. --sprt 20)')
parser.add_argument('--threads', type=int, default=1, help='Number of search threads per engine (default: 1)')
parser.add_argument('--ponder', action='store_true', help='Enable pondering')
parser.add_argument('--games', type=int, help='Override number of games')
parser.add_argument('--ordered', action='store_true', help='Use opening file order instead of random order')

args = parser.parse_args()

concurrency = calc_concurrency(args.threads, args.ponder)

# Engine resolution
if args.engine_a_version is None and args.engine_b_version is None:
    engine_a = BINARY_PATH.resolve()
    engine_b = find_latest_version()
    if engine_b is None:
        print('Error: No versions found in versions directory')
        exit(1)
    engine_b = engine_b.resolve()
    engine_a_name = 'current'
    engine_b_name = engine_b.name

elif args.engine_a_version is not None and args.engine_b_version is None:
    engine_a = BINARY_PATH.resolve()
    engine_b = find_version(args.engine_a_version)
    engine_a_name = 'current'
    engine_b_name = engine_b.name

elif args.engine_a_version is not None and args.engine_b_version is not None:
    engine_a = find_version(args.engine_a_version)
    engine_b = find_version(args.engine_b_version)
    engine_a_name = engine_a.name
    engine_b_name = engine_b.name

# Build command based on mode
if args.sprt:
    max_games = args.games if args.games is not None else SPRT_MAX_GAMES
    test_type = f'SPRT (elo1={args.sprt})'
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        args.threads, args.ponder, concurrency,
        sprt={'elo0': 0, 'elo1': args.sprt, 'alpha': SPRT_ALPHA, 'beta': SPRT_BETA},
        max_games=max_games,
        ordered=args.ordered,
    )
else:
    max_games = args.games if args.games is not None else STANDARD_MAX_GAMES
    test_type = 'Standard'
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        args.threads, args.ponder, concurrency,
        max_games=max_games,
        ordered=args.ordered,
    )

print_config(test_type, engine_a_name, engine_b_name, args.threads, args.ponder, concurrency, max_games)

# Print command and confirm run
print("Command:")
print(' '.join(str(c) for c in cmd))
confirm = input('\nConfirm (y/N): ')
if confirm.lower() != 'y':
    print('Aborted')
    exit(0)

# Execute cutechess-cli
print('\n', flush=True)
subprocess.run(cmd, check=True)
