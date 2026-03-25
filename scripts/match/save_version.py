#!/usr/bin/env python3
"""
Saves the current build binary to the versions folder with an incrementing version prefix.
If no name is provided, overwrites the latest version.
"""

import argparse
import re
import shutil

from lib.path import BINARY_PATH
from match.version import find_latest_version, VERSIONS_DIR


def resolve_filename(name, latest):
    """Determine the output filename: auto-increment if name given, otherwise confirm overwrite."""
    latest_version = int(re.match(r'v(\d+)_', latest.name).group(1)) if latest else -1

    if name:
        return f'v{latest_version + 1}_{name}'

    if latest is None:
        print('Error: No existing version to overwrite')
        exit(1)

    response = input(f'Overwrite {latest.name}? (y/N): ')
    if response.lower() != 'y':
        print('Aborted')
        exit(0)

    return latest.name


def main():
    """Copy the current build binary to the versions directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('name', nargs='?', help='version name (omit to overwrite latest)')
    args = parser.parse_args()

    latest = find_latest_version()
    new_filename = resolve_filename(args.name, latest)

    VERSIONS_DIR.mkdir(exist_ok=True)
    shutil.copy2(BINARY_PATH, VERSIONS_DIR / new_filename)
    print(f'Saved binary as: {new_filename}')


if __name__ == '__main__':
    main()
