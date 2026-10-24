#!/usr/bin/env bash
# Plattformübergreifendes Build-Skript; Abhängigkeiten müssen installiert sein.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${IX_BUILD_DIR:-$project_dir/build/native}"
cmake -S "$project_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "$@"
cmake --build "$build_dir" --parallel "${IX_BUILD_JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure
