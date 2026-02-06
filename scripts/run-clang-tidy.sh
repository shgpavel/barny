#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:?Usage: $0 <build_dir> <source_dir>}"
SOURCE_DIR="${2:?Usage: $0 <build_dir> <source_dir>}"

if ! command -v run-clang-tidy &>/dev/null; then
    echo "run-clang-tidy not found, skipping"
    exit 77
fi

run-clang-tidy \
    -p "$BUILD_DIR" \
    -source-filter='.*src/.*\.c$' \
    -warnings-as-errors='*' \
    -quiet
