#!/usr/bin/env python3
"""
Helpers for finding and resolving versioned engine binaries in the versions directory.
"""

import re

from lib.path import PROJECT_ROOT

VERSIONS_DIR = PROJECT_ROOT / 'versions'


def find_version(prefix):
    """Find a single version binary matching a prefix (e.g. 'v1')."""
    matches = [f for f in VERSIONS_DIR.iterdir() if f.name.startswith(f'{prefix}_')] if VERSIONS_DIR.exists() else []

    if len(matches) == 0:
        print(f'Error: No binary found for version "{prefix}"')
        exit(1)
    elif len(matches) > 1:
        print(f'Error: Multiple binaries found for version "{prefix}"')
        for m in matches:
            print(f'  {m}')
        exit(1)

    return matches[0].resolve()


def find_latest_version():
    """Find the binary with the highest version number."""
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

    return latest_file
