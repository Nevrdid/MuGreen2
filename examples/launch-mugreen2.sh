#!/bin/sh
# Example launcher for a handheld or desktop installation.
set -eu

if [ "$#" -ne 1 ]; then
    printf "Usage: %s <PDF file>\n" "$0" >&2
    exit 2
fi

# Set MUGREEN2_DISPLAY_ROTATION to 180 for an upside-down panel.
display_rotation="${MUGREEN2_DISPLAY_ROTATION:-0}"
exec "${MUGREEN2_BIN:-mugreen2}" "-display-rotation=$display_rotation" "$1"
