#!/bin/bash

# Usage: ./code_style.sh
# Usage: ./code_style.sh --check

if [[ "x$1" == *"check"* ]]; then
  echo "checking code format"
else
  fix=1
fi

root="$(git rev-parse --show-toplevel)"
cd "${root}" || exit 1

skipfiles=(
  libtransmission/ConvertUTF\.[ch]
  libtransmission/jsonsl\.[ch]
  libtransmission/wildmat\.c
)
candidates=(
  cli/*\.[ch]
  daemon/*\.[ch]
  gtk/*\.[ch]
  libtransmission/*\.[ch]
  qt/*\.cc
  qt/*\.h
  tests/*/*\.cc
  tests/*/*\.h
  utils/*\.[ch]
)
for file in "${candidates[@]}"; do
  if [[ ! " ${skipfiles[*]} " =~ ${file} ]]; then
    cfiles+=("${file}");
  fi
done

# format C/C++
if [ -n "$fix"  ]; then
  ./run-clang-format.py --in-place "${cfiles[@]}"
elif ! ./run-clang-format.py --quiet "${cfiles[@]}"; then
  echo 'C/C++ code needs formatting'
  exitcode=1
fi

# enforce east const
matches="$(grep --line-number --with-filename -P '((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)' "${cfiles[@]}")"
if [ -n "${matches}" ]; then
  echo 'const in wrong place:'
  echo "${matches}"
  exitcode=1
fi
if [ -n "$fix"  ]; then
  perl -pi -e 's/((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/\1>\2</g' "${cfiles[@]}"
fi

# format JS
cd "${root}/web" || exit 1
if [ -n "$fix"  ]; then
  cd "${root}/web" && yarn --silent install && yarn --silent lint:fix
elif ! yarn -s install && yarn --silent lint; then
  echo 'JS code needs formatting'
  exitcode=1
fi

exit $exitcode
