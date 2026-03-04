#!/usr/bin/env python3

import argparse
import subprocess
import re

from paths import CUTECHESS_CLI_BINARY_PATH, VERSIONS_DIR, BINARY_PATH, OPENINGS_PATH, require_env

require_env('cutechess_cli_binary')

# Fixed settings
TC = '8+0.08'
CONCURRENCY = 8
TIMEMARGIN = 50
MAX_GAMES = 1000
DRAW_MOVENUMBER = 60
DRAW_MOVECOUNT = 8
DRAW_SCORE = 5
RESIGN_MOVECOUNT = 8
RESIGN_SCORE = 600
SPRT_ALPHA = 0.05
SPRT_BETA = 0.05

# Debug preset
DEBUG_TC = '60+0'
DEBUG_GAMES = 2

# Custom defaults
CUSTOM_DEFAULT_TC = '8+0.08'
CUSTOM_DEFAULT_GAMES = 100


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


def build_cmd(engine_a, engine_b, engine_a_name, engine_b_name, **options):
    use_book = options.pop('use_book', False)
    cmd = [
        str(CUTECHESS_CLI_BINARY_PATH),
        '-engine', f'cmd={engine_a}', f'name={engine_a_name}',
        '-engine', f'cmd={engine_b}', f'name={engine_b_name}',
        '-each', 'proto=uci', 'ponder=on', f'option.OwnBook={"true" if use_book else "false"}'
    ]

    if 'tc' in options:
        cmd.append(f'tc={options["tc"]}')

    if 'timemargin' in options:
        cmd.append(f'timemargin={options["timemargin"]}')

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

    if 'sprt' in options:
        sprt = options['sprt']
        cmd.extend([
            '-sprt',
            f'elo0={sprt["elo0"]}',
            f'elo1={sprt["elo1"]}',
            f'alpha={sprt["alpha"]}',
            f'beta={sprt["beta"]}'
        ])

    if 'games' in options:
        cmd.extend(['-games', str(options['games'])])

    if options.get('repeat', False):
        cmd.append('-repeat')

    if 'concurrency' in options:
        cmd.extend(['-concurrency', str(options['concurrency'])])

    if options.get('debug', False):
        cmd.append('-debug')

    if not use_book:
        cmd.extend(['-openings', f'file={OPENINGS_PATH}', 'format=pgn', 'order=random'])

    return cmd


def print_config(test_type, engine_a_name, engine_b_name, **kwargs):
    print(f'\n{"="*60}')

    print(f'Test Type: {test_type}')
    print(f'Engine A: {engine_a_name}')
    print(f'Engine B: {engine_b_name}')

    for key, value in kwargs.items():
        print(f'{key}: {value}')

    print(f'{"="*60}\n')


parser = argparse.ArgumentParser(
    description='Run SPRT test between engine versions using cutechess-cli'
)

# Engine selection
parser.add_argument('engine_a_version', nargs='?', help='Engine A version (e.g. v1) - optional')
parser.add_argument('engine_b_version', nargs='?', help='Engine B version (e.g. v2) - optional')

# Mode selection
mode_group = parser.add_mutually_exclusive_group(required=True)
mode_group.add_argument('--elo', type=int, help='Expected Elo gain for SPRT (sets elo1, e.g. --elo 20)')
mode_group.add_argument('--debug', action='store_true', help='Run debug mode (2 games, no adjudication)')
mode_group.add_argument('--custom', action='store_true', help='Run custom test with specified parameters')

# Custom mode parameters
parser.add_argument('-t', '--tc', type=str, help='Time control as time+inc (custom mode only)')
parser.add_argument('-g', '--games', type=int, help='Number of games (custom mode only)')
parser.add_argument('--use-book', action='store_true', help='Use engine\'s own opening book instead of cutechess openings')

args = parser.parse_args()

# Guard: custom parameters cannot be used with non-custom modes
if not args.custom:
    if any([args.tc, args.games]):
        print('Error: Cannot specify custom parameters (-t, -g) when using --elo or --debug')
        exit(1)

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
if args.elo:
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=TC,
        timemargin=TIMEMARGIN,
        draw={'movenumber': DRAW_MOVENUMBER, 'movecount': DRAW_MOVECOUNT, 'score': DRAW_SCORE},
        resign={'movecount': RESIGN_MOVECOUNT, 'score': RESIGN_SCORE},
        sprt={'elo0': 0, 'elo1': args.elo, 'alpha': SPRT_ALPHA, 'beta': SPRT_BETA},
        games=MAX_GAMES,
        repeat=True,
        concurrency=CONCURRENCY,
        use_book=args.use_book
    )

    print_config(
        f'SPRT (elo1={args.elo})',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': TC,
            'Timemargin': f'{TIMEMARGIN}ms',
            'Max Games': MAX_GAMES,
            'SPRT': f'elo0=0, elo1={args.elo}, alpha={SPRT_ALPHA}, beta={SPRT_BETA}',
            'Draw': f'movenumber={DRAW_MOVENUMBER}, movecount={DRAW_MOVECOUNT}, score={DRAW_SCORE}',
            'Resign': f'movecount={RESIGN_MOVECOUNT}, score={RESIGN_SCORE}',
            'Concurrency': CONCURRENCY
        }
    )

elif args.debug:
    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=DEBUG_TC,
        games=DEBUG_GAMES,
        debug=True
    )

    print_config(
        'Debug',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': DEBUG_TC,
            'Games': DEBUG_GAMES,
            'Note': 'No adjudication, concurrency, or randomization'
        }
    )

else:  # custom mode
    tc = args.tc if args.tc is not None else CUSTOM_DEFAULT_TC
    games = args.games if args.games is not None else CUSTOM_DEFAULT_GAMES

    cmd = build_cmd(
        engine_a, engine_b, engine_a_name, engine_b_name,
        tc=tc,
        timemargin=TIMEMARGIN,
        games=games,
        repeat=True,
        concurrency=CONCURRENCY,
        use_book=args.use_book
    )

    tc_str = tc + (' (default)' if args.tc is None else '')
    games_str = str(games) + (' (default)' if args.games is None else '')

    print_config(
        'Custom',
        engine_a_name,
        engine_b_name,
        **{
            'Time Control': tc_str,
            'Games': games_str,
            'Concurrency': CONCURRENCY,
            'Timemargin': f'{TIMEMARGIN}ms'
        }
    )

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
