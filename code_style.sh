#!/usr/bin/env bash

set -euo pipefail

[ -z "${1:-}" ] || cd "$1"

echo '=================='
echo '=== uncrustify ==='
echo '=================='
echo ''

find \
        cli \
        daemon \
        gtk \
        libtransmission \
        utils \
    \( -name '*.c' -o -name '*.h' \) \
    ! \( -name 'ConvertUTF.*' -o -name 'jsonsl.*' -o -name 'wildmat.c' \) \
    -print0 |
xargs \
    -0 \
    uncrustify \
        --replace \
        --no-backup \
        -c uncrustify.cfg

find \
        qt \
        tests \
    \( -name '*.cc' -o -name '*.h' \) \
    -print0 |
xargs \
    -0 \
    uncrustify \
        --replace \
        --no-backup \
        -l CPP \
        -c uncrustify.cfg

echo ''
echo '================================================================='
echo '=== const placement (until uncrustify supports it, hopefully) ==='
echo '================================================================='
echo ''

find \
        cli \
        daemon \
        gtk \
        libtransmission \
        qt \
        utils \
    \( -name '*.c' -o -name '*.cc' -o -name '*.h' \) \
    ! \( -name 'ConvertUTF.*' -o -name 'jsonsl.*' -o -name 'wildmat.c' \) \
    -print0 |
xargs \
    -0 \
    -n1 \
    perl \
        -pi \
        -e 'BEGIN { print STDOUT "Processing: ${ARGV[0]}\n" } s/((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/\1>\2</g'

echo ''
echo '==================='
echo '=== js-beautify ==='
echo '==================='
echo ''

find \
        web \
    ! -path '*/jquery/*' \
    -name '*.js' \
    -print0 |
xargs \
    -0 \
    js-beautify \
        --config .jsbeautifyrc \
        --replace
