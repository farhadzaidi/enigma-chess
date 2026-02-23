import argparse
import shutil
import re

from paths import VERSIONS_DIR, BINARY_PATH

# Use parser to extract command-line arguments
parser = argparse.ArgumentParser(
    description="Copies the current binary (./build/enigma) to the versions folder with the provided name prefixed with incrementing version number (e.g. v1_{name})"
)
parser.add_argument("name", nargs="?", help="the name to assign the binary (optional - if not provided, overwrites latest)")
args = parser.parse_args()

# Find the latest version in the directory
latest_version = -1
latest_filename = None
if VERSIONS_DIR.exists():
    for file in VERSIONS_DIR.iterdir():
        match = re.match(r'v(\d+)_(.+)', file.name)
        if match:
            version_num = int(match.group(1))
            if version_num > latest_version:
                latest_version = version_num
                latest_filename = file.name

# Determine if overwriting or creating new version
if args.name:
    # Create new version
    new_version = latest_version + 1
    new_filename = f"v{new_version}_{args.name}"
else:
    # Overwrite latest version
    if latest_filename is None:
        print("Error: No existing version to overwrite")
        exit(1)

    # Ask for confirmation
    response = input(f"Overwrite {latest_filename}? (y/N): ")
    if response.lower() != 'y':
        print("Aborted")
        exit(0)

    new_filename = latest_filename

dest_path = VERSIONS_DIR / new_filename

# Create versions directory if it doesn't exist
VERSIONS_DIR.mkdir(exist_ok=True)

# Copy the binary
shutil.copy2(BINARY_PATH, dest_path)
print(f"Saved binary as: {new_filename}")
