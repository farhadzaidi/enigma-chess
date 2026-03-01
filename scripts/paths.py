import os
from pathlib import Path


def _env_path(name):
    value = os.environ.get(name)
    if value is None:
        return None
    return Path(value)


def require_env(name):
    path = _env_path(name)
    if path is None:
        print(f'Error: Environment variable "{name}" is not set')
        exit(1)
    return path


CUTECHESS_CLI_BINARY_PATH = _env_path('cutechess_cli_binary')
BINARY_PATH = Path('build') / 'enigma'
VERSIONS_DIR = Path('versions')
OPENINGS_PATH = Path('fen') / 'openings.pgn'
