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

# We're targeting clang-format version 20 and other versions give slightly
# different results, so prefer `clang-format-20` if it's installed.
clang_format_exe_names=(
  'clang-format-20'
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

# format Xcodeproj
if ! grep -q 'objectVersion = 51' Transmission.xcodeproj/project.pbxproj; then
  echo 'project.pbxproj needs objectVersion = 51 for compatibility with Xcode 11'
  exitcode=1
fi
if ! grep -q 'BuildIndependentTargetsInParallel = YES' Transmission.xcodeproj/project.pbxproj; then
  echo 'please keep BuildIndependentTargetsInParallel in project.pbxproj'
  exitcode=1
fi

# format JS
# but only if js has changed
git diff --cached --quiet -- "web/**" && exit $exitcode
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
