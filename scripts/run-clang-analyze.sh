#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:?Usage: $0 <build_dir> <source_dir>}"
SOURCE_DIR="${2:?Usage: $0 <build_dir> <source_dir>}"

if ! command -v clang &>/dev/null; then
    echo "clang not found, skipping"
    exit 77
fi

COMPILE_DB="$BUILD_DIR/compile_commands.json"
if [ ! -f "$COMPILE_DB" ]; then
    echo "compile_commands.json not found at $COMPILE_DB"
    exit 1
fi

errors=0

# Use Python to robustly parse compile_commands.json and extract flags
while IFS=$'\t' read -r directory file flags; do
    echo "Analyzing: $file"
    # shellcheck disable=SC2086
    output=$(cd "$directory" && clang --analyze \
        -Xanalyzer -analyzer-output=text \
        $flags \
        "$file" 2>&1) || true
    if [ -n "$output" ]; then
        echo "$output"
        if echo "$output" | grep -qE 'warning:|error:'; then
            errors=$((errors + 1))
        fi
    fi
done < <(python3 -c "
import json, os, shlex, sys

with open('$COMPILE_DB') as f:
    db = json.load(f)

# Flags to skip entirely (with their argument)
SKIP_WITH_ARG = {'-o', '-MF', '-MQ', '-MT'}
# Flags to skip (standalone)
SKIP_STANDALONE = {'-c', '-MD', '-MMD', '-MP'}

seen = set()
for entry in db:
    src = entry['file']
    # Only analyze src/ files
    if '/src/' not in src and not src.startswith('src/') and not src.startswith('../src/'):
        continue

    # Resolve to absolute path for deduplication
    abs_src = os.path.normpath(os.path.join(entry['directory'], src))
    if abs_src in seen:
        continue
    seen.add(abs_src)

    directory = entry['directory']
    parts = shlex.split(entry['command'])

    # Drop compiler (and ccache wrapper if present)
    # parts[0] is the compiler or ccache; if ccache, parts[1] is the real compiler
    start = 1
    if 'ccache' in parts[0]:
        start = 2

    flags = []
    i = start
    while i < len(parts):
        arg = parts[i]
        if arg in SKIP_WITH_ARG:
            i += 2  # skip flag and its argument
            continue
        if arg in SKIP_STANDALONE:
            i += 1
            continue
        if arg == src:
            i += 1
            continue
        flags.append(arg)
        i += 1

    print(f'{directory}\t{src}\t{shlex.join(flags)}')
")

if [ "$errors" -gt 0 ]; then
    echo "clang-analyzer: found issues in $errors file(s)"
    exit 1
fi

echo "clang-analyzer: all files clean"
