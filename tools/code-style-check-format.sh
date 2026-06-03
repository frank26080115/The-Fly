#!/usr/bin/env bash
set -euo pipefail

search_dirs=(src include lib test)
ignore_dirs=(
    "lib/arduino-liblame"
    "lib/libhelix-esp32-arduino"
    "lib/ShineWrapper/shine"
)
ignore_files=(
    "src/sprites.cpp"
    "src/version.c"
    "src/version.cpp"
)

prune_args=()
for dir in "${ignore_dirs[@]}"; do
    prune_args+=( -path "$dir" -o )
done
for file in "${ignore_files[@]}"; do
    prune_args+=( -path "$file" -o )
done

if ((${#prune_args[@]} > 0)); then
    prune_args=("${prune_args[@]:0:${#prune_args[@]}-1}")
    find "${search_dirs[@]}" \
        \( "${prune_args[@]}" \) -prune -o \
        \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
        -print0 | xargs -0 clang-format --dry-run --Werror
else
    find "${search_dirs[@]}" \
        \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
        -print0 | xargs -0 clang-format --dry-run --Werror
fi
