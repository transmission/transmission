#!/usr/bin/env bash
# Code-style pre-commit hook for git. Only checks staged files or whatever is passed in.
set -euo pipefail
shopt -s extglob

base="$(dirname -- "${BASH_SOURCE[0]}")"

uncrust_c() {
    uncrustify \
        --replace \
        --no-backup \
        -c "$base"/uncrustify.cfg \
        -- "$1"
}

uncrust_cxx() {
    uncrustify \
        --replace \
        --no-backup \
        -l CPP \
        -c "$base"/uncrustify.cfg \
        -- "$1"
}

place_const() {
    perl \
        -pi \
        -e 'BEGIN { print STDOUT "Processing: ${ARGV[0]}\n" } s/((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/\1>\2</g' \
        -- "$1"
}

beautify_js() {
    js-beautify \
        --config "$base"/.jsbeautifyrc \
        --replace \
        -- "$1"
}

if (($#)); then
    files=("$@")
else
    # Pre-commit hooks don't get called with any arguments.
    cd -P -- "$base"
    while IFS= read -r -d '' entry; do
        files+=("$entry")
    done < <(git diff --diff-filter=d --cached --name-only -z -- \
            {cli,daemon,gtk,libtransmission,utils}'/*.c' '*.h' 'qt/*.cc' 'web/*.js')
fi

for f in "${files[@]}"; do
    # We aren't dealing with a lot of files here so let's not accumulate file
    # arguments or use parallel jobs for speed yet.
    case "$f" in
        (*/ConvertUTF.*|*/jsonsl.*|*/wildmat.c|*/jquery/*)
            continue;;
        (*.js)
            beautify_js "$1";;
        (@(cli|daemon|gtk|libtransmission|utils)/*.@(c|h))
            uncrust_c "$1"
            place_const "$1";;
        (qt/*.@(cc|h))
            uncrust_cxx "$1"
            place_const "$1";;
    esac
done
