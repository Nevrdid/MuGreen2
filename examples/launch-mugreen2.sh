#!/bin/sh
# Example launcher for a handheld or desktop installation.
set -eu

if [ "$#" -ne 1 ]; then
    printf "Usage: %s <PDF file>\n" "$0" >&2
    exit 2
fi

# Set GREEN_ROTATE180=1 in the environment for an upside-down panel.
export GREEN_ROTATE180="${GREEN_ROTATE180:-0}"
exec "${MUGREEN2_BIN:-mugreen2}" "$1"
