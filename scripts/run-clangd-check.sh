#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:?Usage: $0 <build_dir> <source_dir>}"
SOURCE_DIR="${2:?Usage: $0 <build_dir> <source_dir>}"

if ! command -v clangd &>/dev/null; then
    echo "clangd not found, skipping"
    exit 77
fi

errors=0

while IFS= read -r file; do
    output=$(clangd --check="$file" --compile-commands-dir="$BUILD_DIR" 2>&1) || true
    if echo "$output" | grep -qE '(error|warning):'; then
        echo "=== $file ==="
        echo "$output" | grep -E '(error|warning):'
        errors=$((errors + 1))
    fi
done < <(find "$SOURCE_DIR/src" -name '*.c' -type f; find "$SOURCE_DIR/include" -name '*.h' -type f)

if [ "$errors" -gt 0 ]; then
    echo "clangd-check: found diagnostics in $errors file(s)"
    exit 1
fi

echo "clangd-check: all files clean"
