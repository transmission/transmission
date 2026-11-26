#!/usr/bin/env bash

# Usage: ./code_style.sh [--check] [--force]

set -o noglob

# PATH workaround for SourceTree
# for Intel Mac
PATH="${PATH}:/usr/local/bin"
# for Apple Silicon Mac
PATH="${PATH}:/opt/homebrew/bin"

while [ $# -gt 0 ]; do
  case "$1" in
    --check)
      check=1
      ;;
    --force)
      force=1
      ;;
  esac
  shift
done

if [ -n "$check" ]; then
  echo 'checking code format'
fi

if [ -n "$force" ]; then
  echo 'forcing C/C++ formatting check/fixes regardless of clang-format version'
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
clang_format_version=20
clang_format_exe_names=(
  "clang-format-${clang_format_version}"
  'clang-format'
)
for name in ${clang_format_exe_names[@]}; do
  clang_format_exe=$(command -v "${name}")
  if [ "$?" -eq 0 ]; then
    clang_format_exe="${name}"
    break
  fi
done

# Xcode toolchain lookup
if [ -z "${clang_format_exe}" ]; then
  xcrun=$(command -v xcrun)
  if [ "$?" -eq 0 ]; then
    clang_format_exe=$("$xcrun" --find clang-format)
  fi
fi

if [ -z "${clang_format_exe}" ]; then
  echo "error: clang-format not found";
  exit 1;
fi

# format C/C++
if [ -n "$force" ] || "${clang_format_exe}" --version | grep -qF "version ${clang_format_version}"; then
  if [ -n "$check" ]; then
    clang_format_args=(--dry-run --Werror)
  else
    clang_format_args=(-i)
  fi

  if ! find_cfiles -exec "${clang_format_exe}" "${clang_format_args[@]}" '{}' '+'; then
    [ -n "$check" ] && echo 'C/C++ code needs formatting'
    exitcode=1
  fi
else
  echo "clang-format version is not ${clang_format_version}, skipping C/C++ code formatting checks"
  echo "Run this script again with '--force' to force the formatting checks"
fi

# check important compatibility constraints in Xcode project
if ! grep -q 'objectVersion = 54;' Transmission.xcodeproj/project.pbxproj; then
  echo "project.pbxproj needs 'objectVersion = 54;' for compatibility with Xcode 12"
  exitcode=1
fi
if ! grep -q 'compatibilityVersion = "Xcode 12.0";' Transmission.xcodeproj/project.pbxproj; then
  echo "project.pbxproj needs 'compatibilityVersion = \"Xcode 12.0\";' for compatibility with Xcode 12"
  exitcode=1
fi
if ! grep -q 'BuildIndependentTargetsInParallel = YES;' Transmission.xcodeproj/project.pbxproj; then
  echo "please keep 'BuildIndependentTargetsInParallel = YES;' line in project.pbxproj for faster builds"
  exitcode=1
fi

# format JS
# but only if js has changed
git diff --cached --quiet -- "web/**" && exit $exitcode
cd "${root}/web" || exit 1
npm_lint_args="$([ -z "$check" ] && echo 'lint:fix' || echo 'lint')"
if ! npm ci --no-audit --no-fund --no-progress &>/dev/null; then
  [ -n "$check" ] && echo 'JS code could not be checked -- "npm ci" failed'
  exitcode=1
elif ! npm run --silent $npm_lint_args; then
  [ -n "$check" ] && echo 'JS code needs formatting'
  exitcode=1
fi

exit $exitcode
