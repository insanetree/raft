#!/usr/bin/env bash
set -e

ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"

echo "Formatting repository with clang-format..."

# Number of CPU cores
JOBS=4

git ls-files | grep -E '\.(c|cc|cpp|cxx|h|hpp|hh|m|mm)$' | \
xargs -P "$JOBS" -I{} clang-format -i "{}"

echo "Formatting complete."