#!/usr/bin/env python3

import argparse
import subprocess
import re

from paths import CUTECHESS_CLI_BINARY_PATH, VERSIONS_DIR, BINARY_PATH, OPENINGS_PATH, require_env

require_env('cutechess_cli_binary')

# Shared constants
CONCURRENCY = 8
TIMEMARGIN = 50

# Quick preset (expected 30+ Elo gains)
QUICK_TIME = 10
QUICK_INCREMENT = 0
QUICK_DRAW_MOVENUMBER = 60
QUICK_DRAW_MOVECOUNT = 8
QUICK_DRAW_SCORE = 5
QUICK_RESIGN_MOVECOUNT = 8
QUICK_RESIGN_SCORE = 600
QUICK_SPRT_ELO0 = 0
QUICK_SPRT_ELO1 = 30
QUICK_SPRT_ALPHA = 0.05
QUICK_SPRT_BETA = 0.05
QUICK_GAMES = 1000

# Standard preset (expected ~20 Elo gains)
STANDARD_TIME = 30
STANDARD_INCREMENT = 0
STANDARD_DRAW_MOVENUMBER = 60
STANDARD_DRAW_MOVECOUNT = 8
STANDARD_DRAW_SCORE = 5
STANDARD_RESIGN_MOVECOUNT = 8
STANDARD_RESIGN_SCORE = 600
STANDARD_SPRT_ELO0 = 0
STANDARD_SPRT_ELO1 = 20
STANDARD_SPRT_ALPHA = 0.05
STANDARD_SPRT_BETA = 0.05
STANDARD_GAMES = 2000

# Precise preset (expected ~10 Elo gains)
PRECISE_TIME = 45
PRECISE_INCREMENT = 0
PRECISE_DRAW_MOVENUMBER = 70
PRECISE_DRAW_MOVECOUNT = 10
PRECISE_DRAW_SCORE = 4
PRECISE_RESIGN_MOVECOUNT = 10
PRECISE_RESIGN_SCORE = 700
PRECISE_SPRT_ELO0 = 0
PRECISE_SPRT_ELO1 = 10
PRECISE_SPRT_ALPHA = 0.05
PRECISE_SPRT_BETA = 0.05
PRECISE_GAMES = 5000

# Debug preset
DEBUG_TIME = 60
DEBUG_INCREMENT = 0
DEBUG_GAMES = 2

# Custom defaults
CUSTOM_DEFAULT_TIME = 10
CUSTOM_DEFAULT_INCREMENT = 0
CUSTOM_DEFAULT_GAMES = 100
CUSTOM_DEFAULT_CONCURRENCY = 8
CUSTOM_DEFAULT_TIMEMARGIN = 50


# Find engine binaries matching version prefixes
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


# Find the latest version in the versions directory
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


def build_cmd(engine_a, engine_b, engine_a_name, engine_b_name, **options):
    """
    Build cutechess-cli command with optional components.

    Args:
        **options:
            tc: Time control as 'time+increment' string
            timemargin: Time margin in ms (int)
            draw: Dict with 'movenumber', 'movecount', 'score'
            resign: Dict with 'movecount', 'score'
            sprt: Dict with 'elo0', 'elo1', 'alpha', 'beta'
            games: Number of games (int)
            repeat: Enable repeat flag (bool)
            concurrency: Concurrency level (int)
            debug: Enable debug output (bool)
    """
    cmd = [
        str(CUTECHESS_CLI_BINARY_PATH),
        '-engine', f'cmd={engine_a}', f'name={engine_a_name}',
        '-engine', f'cmd={engine_b}', f'name={engine_b_name}',
        '-each', 'proto=uci', 'ponder=off', 'option.OwnBook=false'
    ]

    # Time control
    if 'tc' in options:
        cmd.append(f'tc={options["tc"]}')

    if 'timemargin' in options:
        cmd.append(f'timemargin={options["timemargin"]}')

    # Adjudication rules
    if 'draw' in options:
        draw = options['draw']
        cmd.extend([
            '-draw',
            f'movenumber={draw["movenumber"]}',
            f'movecount={draw["movecount"]}',
            f'score={draw["score"]}'
        ])

    if 'resign' in options:
        resign = options['resign']
        cmd.extend([
            '-resign',
            f'movecount={resign["movecount"]}',
            f'score={resign["score"]}'
        ])

    # SPRT
    if 'sprt' in options:
        sprt = options['sprt']
        cmd.extend([
            '-sprt',
            f'elo0={sprt["elo0"]}',
            f'elo1={sprt["elo1"]}',
            f'alpha={sprt["alpha"]}',
            f'beta={sprt["beta"]}'
        ])

    # Games and options
    if 'games' in options:
        cmd.extend(['-games', str(options['games'])])

    if options.get('repeat', False):
        cmd.append('-repeat')

    if 'concurrency' in options:
        cmd.extend(['-concurrency', str(options['concurrency'])])

    if options.get('debug', False):
        cmd.append('-debug')

    # Use external opening book for all matches
    cmd.extend(['-openings', f'file={OPENINGS_PATH}', 'format=pgn', 'order=random'])

    return cmd


def print_config(test_type, engine_a_name, engine_b_name, **kwargs):    
    print(f'\n{'='*60}')

    print(f'Test Type: {test_type}')
    print(f'Engine A: {engine_a_name}')
    print(f'Engine B: {engine_b_name}')

    for key, value in kwargs.items():
        print(f'{key}: {value}')

    print(f'{'='*60}\n')


# Use parser to extract command-line arguments
parser = argparse.ArgumentParser(
    description='Uses cutechess-cli to play two versions of the engine against each other'
)

# Engine selection
parser.add_argument('engine_a_version', nargs='?', help='Engine A version (e.g. v1) - optional')
parser.add_argument('engine_b_version', nargs='?', help='Engine B version (e.g. v2) - optional')

# Mode selection (mutually exclusive, required)
mode_group = parser.add_mutually_exclusive_group(required=True)
mode_group.add_argument('--quick', action='store_true', help='Run quick SPRT test for 30+ Elo gains (elo1=30, tc=10+0)')
mode_group.add_argument('--standard', action='store_true', help='Run standard SPRT test for ~20 Elo gains (elo1=20, tc=30+0)')
mode_group.add_argument('--precise', action='store_true', help='Run precise SPRT test for ~10 Elo gains (elo1=10, tc=45+0)')
mode_group.add_argument('--debug', action='store_true', help='Run debug mode (2 games, no adjudication)')
mode_group.add_argument('--custom', action='store_true', help='Run custom test with specified parameters')

# Custom mode parameters (only used with --custom)
parser.add_argument('-t', '--time', type=int, help='Time per side in seconds (custom mode only)')
parser.add_argument('-i', '--increment', type=float, help='Increment per move in seconds (custom mode only)')
parser.add_argument('-g', '--games', type=int, help='Number of games (custom mode only)')
parser.add_argument('-c', '--concurrency', type=int, help='Concurrency level (custom mode only)')
parser.add_argument('-m', '--timemargin', type=int, help='Time margin in ms (custom mode only)')

args = parser.parse_args()

# Guard: custom parameters cannot be used with preset modes
if not args.custom:
    if any([args.time, args.increment, args.games, args.concurrency, args.timemargin]):
        print('Error: Cannot specify custom parameters (-t, -i, -g, -c, -m) when using a preset mode')
        exit(1)

# Determine mode
if args.quick:
    mode = 'quick'
elif args.standard:
    mode = 'standard'
elif args.precise:
    mode = 'precise'
elif args.debug:
    mode = 'debug'
else:
    mode = 'custom'

# Engine resolution
if args.engine_a_version is None and args.engine_b_version is None:
    # No args: current vs latest
    engine_a = BINARY_PATH.resolve()
    engine_b = find_latest_version()
    engine_a_name = 'current'
    engine_b_name = engine_b.name

elif args.engine_a_version is not None and args.engine_b_version is None:
    # One arg: current vs specified
    engine_a = BINARY_PATH.resolve()
    engine_b = find_binary(args.engine_a_version)
    engine_a_name = 'current'
    engine_b_name = engine_b.name

elif args.engine_a_version is not None and args.engine_b_version is not None:
    # Two args: version vs version
    engine_a = find_binary(args.engine_a_version)
    engine_b = find_binary(args.engine_b_version)
    engine_a_name = engine_a.name
    engine_b_name = engine_b.name

# Build command based on mode
if mode == 'quick':
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=f'{QUICK_TIME}+{QUICK_INCREMENT}',
        timemargin=TIMEMARGIN,
        draw={'movenumber': QUICK_DRAW_MOVENUMBER, 'movecount': QUICK_DRAW_MOVECOUNT, 'score': QUICK_DRAW_SCORE},
        resign={'movecount': QUICK_RESIGN_MOVECOUNT, 'score': QUICK_RESIGN_SCORE},
        sprt={'elo0': QUICK_SPRT_ELO0, 'elo1': QUICK_SPRT_ELO1, 'alpha': QUICK_SPRT_ALPHA, 'beta': QUICK_SPRT_BETA},
        games=QUICK_GAMES,
        repeat=True,
        concurrency=CONCURRENCY
    )

    print_config(
        'Quick SPRT (30+ Elo)',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': f'{QUICK_TIME}+{QUICK_INCREMENT}',
            'Timemargin': f'{TIMEMARGIN}ms',
            'Games': QUICK_GAMES,
            'SPRT': f'elo0={QUICK_SPRT_ELO0}, elo1={QUICK_SPRT_ELO1}, alpha={QUICK_SPRT_ALPHA}, beta={QUICK_SPRT_BETA}',
            'Draw': f'movenumber={QUICK_DRAW_MOVENUMBER}, movecount={QUICK_DRAW_MOVECOUNT}, score={QUICK_DRAW_SCORE}',
            'Resign': f'movecount={QUICK_RESIGN_MOVECOUNT}, score={QUICK_RESIGN_SCORE}',
            'Concurrency': CONCURRENCY
        }
    )

elif mode == 'standard':
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=f'{STANDARD_TIME}+{STANDARD_INCREMENT}',
        timemargin=TIMEMARGIN,
        draw={'movenumber': STANDARD_DRAW_MOVENUMBER, 'movecount': STANDARD_DRAW_MOVECOUNT, 'score': STANDARD_DRAW_SCORE},
        resign={'movecount': STANDARD_RESIGN_MOVECOUNT, 'score': STANDARD_RESIGN_SCORE},
        sprt={'elo0': STANDARD_SPRT_ELO0, 'elo1': STANDARD_SPRT_ELO1, 'alpha': STANDARD_SPRT_ALPHA, 'beta': STANDARD_SPRT_BETA},
        games=STANDARD_GAMES,
        repeat=True,
        concurrency=CONCURRENCY
    )

    print_config(
        'Standard SPRT (~20 Elo)',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': f'{STANDARD_TIME}+{STANDARD_INCREMENT}',
            'Timemargin': f'{TIMEMARGIN}ms',
            'Games': STANDARD_GAMES,
            'SPRT': f'elo0={STANDARD_SPRT_ELO0}, elo1={STANDARD_SPRT_ELO1}, alpha={STANDARD_SPRT_ALPHA}, beta={STANDARD_SPRT_BETA}',
            'Draw': f'movenumber={STANDARD_DRAW_MOVENUMBER}, movecount={STANDARD_DRAW_MOVECOUNT}, score={STANDARD_DRAW_SCORE}',
            'Resign': f'movecount={STANDARD_RESIGN_MOVECOUNT}, score={STANDARD_RESIGN_SCORE}',
            'Concurrency': CONCURRENCY
        }
    )

elif mode == 'precise':
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=f'{PRECISE_TIME}+{PRECISE_INCREMENT}',
        timemargin=TIMEMARGIN,
        draw={'movenumber': PRECISE_DRAW_MOVENUMBER, 'movecount': PRECISE_DRAW_MOVECOUNT, 'score': PRECISE_DRAW_SCORE},
        resign={'movecount': PRECISE_RESIGN_MOVECOUNT, 'score': PRECISE_RESIGN_SCORE},
        sprt={'elo0': PRECISE_SPRT_ELO0, 'elo1': PRECISE_SPRT_ELO1, 'alpha': PRECISE_SPRT_ALPHA, 'beta': PRECISE_SPRT_BETA},
        games=PRECISE_GAMES,
        repeat=True,
        concurrency=CONCURRENCY
    )

    print_config(
        'Precise SPRT (~10 Elo)',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': f'{PRECISE_TIME}+{PRECISE_INCREMENT}',
            'Timemargin': f'{TIMEMARGIN}ms',
            'Games': PRECISE_GAMES,
            'SPRT': f'elo0={PRECISE_SPRT_ELO0}, elo1={PRECISE_SPRT_ELO1}, alpha={PRECISE_SPRT_ALPHA}, beta={PRECISE_SPRT_BETA}',
            'Draw': f'movenumber={PRECISE_DRAW_MOVENUMBER}, movecount={PRECISE_DRAW_MOVECOUNT}, score={PRECISE_DRAW_SCORE}',
            'Resign': f'movecount={PRECISE_RESIGN_MOVECOUNT}, score={PRECISE_RESIGN_SCORE}',
            'Concurrency': CONCURRENCY
        }
    )

elif mode == 'debug':
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=f'{DEBUG_TIME}+{DEBUG_INCREMENT}',
        games=DEBUG_GAMES,
        debug=True
    )

    print_config(
        'Debug',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': f'{DEBUG_TIME}+{DEBUG_INCREMENT}',
            'Games': DEBUG_GAMES,
            'Note': 'No adjudication, concurrency, or randomization'
        }
    )

else:  # custom mode
    # Apply defaults with fallback
    time = args.time if args.time is not None else CUSTOM_DEFAULT_TIME
    increment = args.increment if args.increment is not None else CUSTOM_DEFAULT_INCREMENT
    games = args.games if args.games is not None else CUSTOM_DEFAULT_GAMES
    concurrency = args.concurrency if args.concurrency is not None else CUSTOM_DEFAULT_CONCURRENCY
    timemargin = args.timemargin if args.timemargin is not None else CUSTOM_DEFAULT_TIMEMARGIN

    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=f'{time}+{increment}',
        timemargin=timemargin,
        games=games,
        repeat=True,
        concurrency=concurrency
    )

    # Build display strings with "(default)" indicators
    time_str = f'{time}+{increment}' + (' (default)' if args.time is None and args.increment is None else '')
    games_str = str(games) + (' (default)' if args.games is None else '')
    concurrency_str = str(concurrency) + (' (default)' if args.concurrency is None else '')
    timemargin_str = f'{timemargin}ms' + (' (default)' if args.timemargin is None else '')

    print_config(
        'Custom',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': time_str,
            'Games': games_str,
            'Concurrency': concurrency_str,
            'Timemargin': timemargin_str
        }
    )

# Print command and confirm run
print("Command:")
print(' '.join(cmd))
confirm = input('\nConfirm (y/N): ')
if confirm.lower() != 'y':
    print('Aborted')
    exit(0)

# Execute cutechess-cli
print('\n', flush=True)
subprocess.run(cmd, check=True)
