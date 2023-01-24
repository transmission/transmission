#!/usr/bin/env bash

# Usage: ./code_style.sh
# Usage: ./code_style.sh --check

set -o noglob

# PATH workaround for SourceTree
# for Intel Mac
PATH="${PATH}:/usr/local/bin"
# for Apple Silicon Mac
PATH="${PATH}:/opt/homebrew/bin"

if [[ "x$1" == *"check"* ]]; then
  echo "checking code format"
else
  fix=1
fi

root="$(dirname "$0")"
root="$(cd "${root}" && pwd)"
cd "${root}" || exit 1

trim_comments() {
  # 1. remove comments, ignoring backslash-escaped characters
  # 2. trim spaces
  # 3. remove empty lines
  # note: GNU extensions like +?| aren't supported on macOS
  sed 's/^\(\(\(\\.\)*[^\\#]*\)*\)#.*/\1/;s/^[[:blank:]]*//;s/[[:blank:]]*$//;/^$/d' "$@"
}

get_find_path_args() {
  local args=$(printf " -o -path ./%s" "$@")
  echo "${args:4}"
}

find_cfiles() {
  # We use the same files as Meson: https://mesonbuild.com/Code-formatting.html
  find . \( $(get_find_path_args $(trim_comments .clang-format-include)) \)\
       ! \( $(get_find_path_args $(trim_comments .clang-format-ignore)) \) "$@"
}

# We're targeting clang-format version 15 and other versions give slightly
# different results, so prefer `clang-format-15` if it's installed.
clang_format_exe_names=(
  'clang-format-15'
  'clang-format'
)
for name in ${clang_format_exe_names[@]}; do
  clang_format_exe=$(command -v "${name}")
  if [ "$?" -eq 0 ]; then
    clang_format_exe="${name}"
    break
  fi
done
if [ -z "${clang_format_exe}" ]; then
  echo "error: clang-format not found";
  exit 1;
fi

# format C/C++
clang_format_args="$([ -n "$fix" ] && echo '-i' || echo '--dry-run --Werror')"
if ! find_cfiles -exec "${clang_format_exe}" $clang_format_args '{}' '+'; then
  [ -n "$fix" ] || echo 'C/C++ code needs formatting'
  exitcode=1
fi

# enforce east const
matches="$(find_cfiles -exec perl -ne 'print "west const:",$ARGV,":",$_ if /((?:^|[(<,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/' '{}' '+')"
if [ -n "$matches" ]; then
  echo "$matches"
  exitcode=1
fi
if [ -n "$fix" ]; then
  find_cfiles -exec perl -pi -e 's/((?:^|[(<,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/\1>\2</g' '{}' '+'
fi

# format JS
cd "${root}/web" || exit 1
npm_lint_args="$([ -n "$fix" ] && echo 'lint:fix' || echo 'lint')"
if ! npm ci --no-audit --no-fund --no-progress &>/dev/null; then
  [ -n "$fix" ] || echo 'JS code could not be checked -- "npm ci" failed'
  exitcode=1
elif ! npm run --silent $npm_lint_args; then
  [ -n "$fix" ] || echo 'JS code needs formatting'
  exitcode=1
fi

exit $exitcode
