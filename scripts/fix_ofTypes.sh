#!/bin/bash

# Check if the system is aarch64
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ]; then
    echo "Error: This script is intended to be run on aarch64 systems only. Current architecture is $ARCH."
    exit 1
fi

echo "WARNING: This script will modify the openFrameworks core file 'libs/openFrameworks/types/ofTypes.h'"
echo "It adds a missing include for <memory> which is required on aarch64 to fix build errors."
read -p "Do you want to proceed? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborting."
    exit 1
fi

# Get the script directory and resolve the OF_ROOT relative to it
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
OF_ROOT="$SCRIPT_DIR/../../../.."
TARGET_FILE="$OF_ROOT/libs/openFrameworks/types/ofTypes.h"

# Make sure the target file exists
if [ ! -f "$TARGET_FILE" ]; then
    echo "Error: Target file not found at $TARGET_FILE"
    echo "Make sure this script is placed in the 'scripts' directory of your project."
    exit 1
fi

# Check if the include has already been added
if grep -q "#include <memory>" "$TARGET_FILE"; then
    echo "The file $TARGET_FILE already contains '#include <memory>'. No changes made."
    exit 0
fi

# Apply the fix using sed to append #include <memory> after #include <mutex>
sed -i '/#include <mutex>/a #include <memory>' "$TARGET_FILE"

echo "Fix applied successfully."
