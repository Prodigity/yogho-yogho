#!/bin/sh
# Compile-only check from bash: ./check.sh [src/a.c src/b.c ...]
args=""
for f in "$@"; do args="$args $(echo "$f" | sed 's|/|\\|g')"; done
powershell -NoProfile -Command "& 'C:\Claude\Yogho yogho\port\check.bat' $args" 2>&1 | grep -v "^[a-z_0-9]*\.c$"
