#!/usr/bin/env bash
set -euo pipefail

find src include lib test \
    \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    -print0 | xargs -0 clang-format -i
