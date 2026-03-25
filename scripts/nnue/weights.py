#!/usr/bin/env python3
"""
Helpers for finding and resolving versioned NNUE weight files.
"""

import re

from nnue.paths import WEIGHTS_DIR, WEIGHTS_GLOB


def _weights_path(n):
    """Return the path for weight file number n."""
    return WEIGHTS_DIR / f'weights_{n}.pt'


def resolve_weights(n=None):
    """Resolve a weights file path.

    n=None: latest (highest numbered), or None if no files exist.
    n=str/int: specific file (raises FileNotFoundError if missing).
    """
    if n is not None:
        path = _weights_path(int(n))
        if not path.exists():
            raise FileNotFoundError(f'Weights file not found: {path}')
        return path

    return find_latest_weights()


def find_latest_weights():
    """Find the weight file with the highest sequence number, or None."""
    latest_n = -1
    latest_file = None

    if WEIGHTS_DIR.exists():
        for file in WEIGHTS_DIR.iterdir():
            match = re.match(r'weights_(\d+)\.pt$', file.name)
            if match:
                n = int(match.group(1))
                if n > latest_n:
                    latest_n = n
                    latest_file = file

    return latest_file


def next_weights_path():
    """Return the path for the next weights file (highest existing + 1, or 0)."""
    WEIGHTS_DIR.mkdir(parents=True, exist_ok=True)
    latest = find_latest_weights()
    if latest is None:
        return _weights_path(0)
    n = int(re.match(r'weights_(\d+)\.pt$', latest.name).group(1))
    return _weights_path(n + 1)