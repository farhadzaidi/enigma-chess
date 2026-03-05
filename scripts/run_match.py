#!/usr/bin/env python3

import argparse
import subprocess
import re

from paths import CUTECHESS_CLI_BINARY_PATH, VERSIONS_DIR, BINARY_PATH, OPENINGS_PATH, require_env

require_env('cutechess_cli_binary')

# Fixed settings
TC = '8+0.08'
CONCURRENCY = 8
TIMEMARGIN = 150
STANDARD_MAX_GAMES = 1000
SPRT_MAX_GAMES = 5000
DRAW_MOVENUMBER = 60
DRAW_MOVECOUNT = 8
DRAW_SCORE = 5
RESIGN_MOVECOUNT = 8
RESIGN_SCORE = 600
SPRT_ALPHA = 0.05
SPRT_BETA = 0.05


def find_binary(version_prefix):
    matches = []
    if VERSIONS_DIR.exists():
        for file in VERSIONS_DIR.iterdir():
            if file.name.startswith(f'{version_prefix}_'):
                matches.append(file)

    if len(matches) == 0:
        print(f'Error: No binary found for version "{version_prefix}"')
        exit(1)
    elif len(matches) > 1:
        print(f'Error: Multiple binaries found for version "{version_prefix}"')
        for m in matches:
            print(f'  {m}')
        exit(1)

    return matches[0].resolve()


def find_latest_version():
    latest_version = -1
    latest_file = None

    if VERSIONS_DIR.exists():
        for file in VERSIONS_DIR.iterdir():
            match = re.match(r'v(\d+)_(.+)', file.name)
            if match:
                version_num = int(match.group(1))
                if version_num > latest_version:
                    latest_version = version_num
                    latest_file = file

    if latest_file is None:
        print('Error: No versions found in versions directory')
        exit(1)

    return latest_file.resolve()


def build_cmd(engine_a, engine_b, engine_a_name, engine_b_name, sprt=None, max_games=STANDARD_MAX_GAMES):
    cmd = [
        str(CUTECHESS_CLI_BINARY_PATH),
        '-engine', f'cmd={engine_a}', f'name={engine_a_name}',
        '-engine', f'cmd={engine_b}', f'name={engine_b_name}',
        '-each', 'proto=uci', 'ponder=off', 'option.OwnBook=false',
        f'tc={TC}', f'timemargin={TIMEMARGIN}',
        '-draw', f'movenumber={DRAW_MOVENUMBER}', f'movecount={DRAW_MOVECOUNT}', f'score={DRAW_SCORE}',
        '-resign', f'movecount={RESIGN_MOVECOUNT}', f'score={RESIGN_SCORE}',
        '-games', str(max_games),
        '-repeat',
        '-concurrency', str(CONCURRENCY),
        '-openings', f'file={OPENINGS_PATH}', 'format=pgn', 'order=random',
    ]

    if sprt is not None:
        cmd.extend([
            '-sprt',
            f'elo0={sprt["elo0"]}',
            f'elo1={sprt["elo1"]}',
            f'alpha={sprt["alpha"]}',
            f'beta={sprt["beta"]}'
        ])

    return cmd


def print_config(test_type, engine_a_name, engine_b_name, games):
    print(f'\n  {test_type}: {engine_a_name} vs {engine_b_name} ({games} games)\n')


parser = argparse.ArgumentParser(
    description='Run match between engine versions using cutechess-cli'
)

parser.add_argument('engine_a_version', nargs='?', help='Engine A version (e.g. v1) - optional')
parser.add_argument('engine_b_version', nargs='?', help='Engine B version (e.g. v2) - optional')
parser.add_argument('--sprt', type=int, metavar='ELO', help='Run SPRT test with expected Elo gain (e.g. --sprt 20)')

args = parser.parse_args()

# Engine resolution
if args.engine_a_version is None and args.engine_b_version is None:
    engine_a = BINARY_PATH.resolve()
    engine_b = find_latest_version()
    engine_a_name = 'current'
    engine_b_name = engine_b.name

elif args.engine_a_version is not None and args.engine_b_version is None:
    engine_a = BINARY_PATH.resolve()
    engine_b = find_binary(args.engine_a_version)
    engine_a_name = 'current'
    engine_b_name = engine_b.name

elif args.engine_a_version is not None and args.engine_b_version is not None:
    engine_a = find_binary(args.engine_a_version)
    engine_b = find_binary(args.engine_b_version)
    engine_a_name = engine_a.name
    engine_b_name = engine_b.name

# Build command based on mode
if args.sprt:
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        sprt={'elo0': 0, 'elo1': args.sprt, 'alpha': SPRT_ALPHA, 'beta': SPRT_BETA},
        max_games=SPRT_MAX_GAMES,
    )

    print_config(f'SPRT (elo1={args.sprt})', engine_a_name, engine_b_name, SPRT_MAX_GAMES)

else:
    cmd = build_cmd(engine_a, engine_b, engine_a_name, engine_b_name)

    print_config('Standard', engine_a_name, engine_b_name, STANDARD_MAX_GAMES)

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
