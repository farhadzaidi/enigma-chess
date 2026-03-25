#!/usr/bin/env python3
"""
Shared path constants and environment helpers used by other scripts.
"""

import os
from pathlib import Path

PROJECT_ROOT = Path('..')
BINARY_PATH = PROJECT_ROOT / 'build' / 'enigma'


def _env_path(name):
    """Return an environment variable as a Path, or None if unset."""
    value = os.environ.get(name)
    if value is None:
        return None
    return Path(value)


def require_env(name):
    """Return an environment variable as a Path, or exit with an error if unset."""
    path = _env_path(name)
    if path is None:
        print(f'Error: Environment variable "{name}" is not set')
        exit(1)
    return path

