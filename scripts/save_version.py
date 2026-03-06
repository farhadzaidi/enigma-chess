#!/usr/bin/env python3
"""
Saves the current build binary to the versions folder with an incrementing version prefix.
If no name is provided, overwrites the latest version.
"""

import argparse
import re
import shutil

from lib.path import VERSIONS_DIR, BINARY_PATH
from lib.version import find_latest_version

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("name", nargs="?", help="version name (omit to overwrite latest)")
args = parser.parse_args()

latest = find_latest_version()
latest_version = int(re.match(r'v(\d+)_', latest.name).group(1)) if latest else -1

if args.name:
    new_version = latest_version + 1
    new_filename = f"v{new_version}_{args.name}"
else:
    if latest is None:
        print("Error: No existing version to overwrite")
        exit(1)

    response = input(f"Overwrite {latest.name}? (y/N): ")
    if response.lower() != 'y':
        print("Aborted")
        exit(0)

    new_filename = latest.name

dest_path = VERSIONS_DIR / new_filename

# Create versions directory if it doesn't exist
VERSIONS_DIR.mkdir(exist_ok=True)

# Copy the binary
shutil.copy2(BINARY_PATH, dest_path)
print(f"Saved binary as: {new_filename}")
