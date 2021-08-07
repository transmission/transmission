#!/usr/bin/env bash

# Usage: ./code_style.sh
# Usage: ./code_style.sh --check

if [[ "x$1" == *"check"* ]]; then
  echo "checking code format"
else
  fix=1
fi

root="$(git rev-parse --show-toplevel)"
if [ -n "${root}" ]; then
  cd "${root}" || exit 1
fi

skipfiles=(
  libtransmission/ConvertUTF\.[ch]
  libtransmission/jsonsl\.[ch]
  libtransmission/wildmat\.c
)
cfile_candidates=(
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
for file in "${cfile_candidates[@]}"; do
  if [[ ! " ${skipfiles[*]} " =~ ${file} ]]; then
    cfiles+=("${file}");
  fi
done

# format C/C++
cores=$(($(nproc) + 1))
if [ -n "$fix" ]; then
  printf "%s\0" "${cfiles[@]}" | xargs -0 -P$cores -I FILE uncrustify -c uncrustify.cfg --no-backup -q FILE
else
  printf "%s\0" "${cfiles[@]}" | xargs -0 -P$cores -I FILE uncrustify -c uncrustify.cfg --check FILE > /dev/null
  if [ "${PIPESTATUS[1]}" -ne "0" ]; then
    echo 'C/C++ code needs formatting'
    exitcode=1
  fi
fi

# enforce east const
matches="$(perl -ne 'print "west const:",$ARGV,":",$_ if /((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/' "${cfiles[@]}")"
if [ -n "$matches" ]; then
  echo "$matches"
  exitcode=1
fi
if [ -n "$fix" ]; then
  perl -pi -e 's/((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/\1>\2</g' "${cfiles[@]}"
fi

# format JS
cd "${root}/web" || exit 1
if [ -n "$fix" ]; then
  cd "${root}/web" && yarn --silent install && yarn --silent 'lint:fix'
elif ! yarn -s install; then
  echo 'JS code could not be checked -- "yarn install" failed'
  exitcode=1
elif ! yarn --silent lint; then
  echo 'JS code needs formatting'
  exitcode=1
fi

exit $exitcode
