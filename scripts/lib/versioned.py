"""
Versioned file helpers.

Manages numbered files (prefix_0.ext, prefix_1.ext, ...) in a directory.
"""

import pickle
import re
from pathlib import Path


def find_latest(directory, prefix, ext):
    """Find the file with the highest sequence number, or None."""
    if not directory.exists():
        return None

    latest_n = -1
    latest_file = None
    pattern = re.compile(rf'^{re.escape(prefix)}_(\d+){re.escape(ext)}$')

    for f in directory.iterdir():
        match = pattern.match(f.name)
        if match:
            n = int(match.group(1))
            if n > latest_n:
                latest_n = n
                latest_file = f

    return latest_file


def resolve(directory, prefix, ext, n=None):
    """Resolve a versioned file.

    n=None: latest (highest numbered), or None if no files exist.
    n="none": skip loading, returns None.
    n=int/str: specific version (raises FileNotFoundError if missing).
    """
    if isinstance(n, str) and n.lower() == 'none':
        return None

    if n is not None:
        path = directory / f'{prefix}_{int(n)}{ext}'
        if not path.exists():
            raise FileNotFoundError(f'Version not found: {path}')
        return path

    return find_latest(directory, prefix, ext)


def next_path(directory, prefix, ext):
    """Return the path for the next version (latest + 1, or 0)."""
    directory.mkdir(parents=True, exist_ok=True)
    latest = find_latest(directory, prefix, ext)
    if latest is None:
        return directory / f'{prefix}_0{ext}'
    n = int(re.match(rf'{re.escape(prefix)}_(\d+)', latest.name).group(1))
    return directory / f'{prefix}_{n + 1}{ext}'


def save_pickle(directory, prefix, data, path=None):
    """Save data as pickle to the given path, or next version if path is None."""
    if path is None:
        path = next_path(directory, prefix, '.pkl')
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'wb') as f:
        pickle.dump(data, f)
    return path


def load_pickle(path):
    """Load data from a pickle file."""
    with open(path, 'rb') as f:
        return pickle.load(f)