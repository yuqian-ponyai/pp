#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if [[ ! -d build ]]; then
  echo "error: build/ does not exist. Run the initial CMake configure step first." >&2
  exit 1
fi

# shellcheck disable=SC1091
source env.sh

"$repo_root/scripts/download-data.sh"

CPUS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)"
if (( CPUS > 1 )); then
  JOBS=$((CPUS - 1))
else
  JOBS=1
fi

cmake --build build --parallel "$JOBS" "$@"
