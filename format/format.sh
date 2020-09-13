#!/usr/bin/env bash


find_sourcefiles_in_dirs(){
  if [ "$changed" -ne "0" ]; then
    files="$(git diff --name-only -- "${@}")";
  elif [ "$staged" -ne "0" ]; then
    files="$(git diff --name-only --staged -- "${@}")";
  else
    files="$(find "${@}")";
  fi
  # remove skipfiles
  files="$(echo "${files}" | sort | comm -23 - "${root}/format/skipfiles.txt")"
  echo "${files}"
}


# globals

all=0
changed=0
check=0
exitcode=0
staged=0
root="$(git rev-parse --show-toplevel)"
prettier_args=(--config "${root}/format/prettier.config.json" --loglevel warn)
uncrustify_args=(-c "${root}/format/uncrustify.cfg")

# parse command line

for var in "$@"
do
  case "$var" in
    --changed) changed=1;;
    --cached|--staged) staged=1;;
    --check|--test) check=1;;
    --all) all=1;;
  esac
done

if [ "${changed}${staged}${all}" -eq "000" ]; then
  echo "usage: $0 {--all|--changed|--staged} [--check]"
  exit 1
fi

if [ "${check}" -ne "0" ]; then
  prettier_args+=(--check);
  uncrustify_args+=(--check -q);
else
  prettier_args+=(--write)
  uncrustify_args+=(--replace --no-backup)
fi


cd "${root}" || exit 1

# format C/C++ files
formatter='uncrustify'
formatter_args=("${uncrustify_args[@]}")
cish_files=()
if ! command -v "${formatter}" &> /dev/null; then
  echo "skipping $formatter (not found)"
else
  # C
  dirs=(cli daemon gtk libtransmission utils)
  filestr=$(find_sourcefiles_in_dirs "${dirs[@]}") # newline-delimited string
  filestr=$(echo "$filestr" | grep -e "\.[ch]$") # remove non-C files
  IFS=$'\n' read -d '' -ra files <<< "${filestr}"; # convert to array
  if [ ${#files[@]} -ne 0 ]; then
    "${formatter}" "${formatter_args[@]}" -l C "${files[@]}" 1>/dev/null || exitcode=1
    cish_files+=("${files[@]}")
  fi

  # C++
  dirs=(qt tests)
  filestr=$(find_sourcefiles_in_dirs "${dirs[@]}") # newline-delimited string
  filestr=$(echo "$filestr" | grep -e "\.cc$" -e "\.h$") # remove non-C++ files
  IFS=$'\n' read -d '' -ra files <<< "${filestr}"; # convert to array
  if [ ${#files[@]} -ne 0 ]; then
    "${formatter}" "${formatter_args[@]}" -l CPP "${files[@]}" 1>/dev/null || exitcode=1
    cish_files+=("${files[@]}")
  fi
fi

# format JS files
formatter='prettier'
formatter_args=("${prettier_args[@]}")
if ! command -v "${formatter}" &> /dev/null; then
  echo "skipping $formatter (not found)"
else
  dirs=(web)
  filestr=$(find_sourcefiles_in_dirs "${dirs[@]}") # newline-delimited string
  filestr=$(echo "$filestr" | grep -e "\.js$") # remove non-JS files
  IFS=$'\n' read -d '' -ra files <<< "${filestr}"; # convert to array
  if [ ${#files[@]} -ne 0 ]; then
    "${formatter}" "${formatter_args[@]}" "${files[@]}" || exitcode=1
  fi
fi

# check const placement.
# do this manually since neither clang-format nor uncrustify do it
if [ ${#cish_files[@]} -ne 0 ]; then
  if [ "${check}" -ne "0" ]; then
    matches="$(grep --line-number --with-filename -P '((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)' "${cish_files[@]}")"
    if [ -n "${matches}" ]; then
      echo 'const in wrong place:'
      echo "${matches}"
      exitcode=1
    fi
  else
    perl -pi -e 's/((?:^|[(,;]|\bstatic\s+)\s*)\b(const)\b(?!\s+\w+\s*\[)/\1>\2</g' "${cish_files[@]}"
  fi
fi

if [ "${check}" -ne "0" ]; then
  if [ "${exitcode}" -eq "1" ]; then
    echo "style check failed. re-run format/format.sh without --check to reformat."
  fi
fi


exit $exitcode
