#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake -B "${ROOT_DIR}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${ROOT_DIR}/build"
