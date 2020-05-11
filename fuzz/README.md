# libtransmission Fuzz Targets

## Requirements

 - A modern version of LLVM / Clang with `libfuzzer` support.

## Build setup

```bash
export CC=clang
export CXX=clang++
export CFLAGS="-g -O0"
```

## Build targets

```bash
mkdir -p build
cd build
cmake .. -DENABLE_FUZZ=ON

# Magnet URI fuzzing
make fuzz-magnet

# Torrent file fuzzing
make fuzz-torrent
```

## Running fuzzers

```bash
# Run torrent fuzzer until a memory error occurs
./fuzz/fuzz-torrent

# Run magnet URI fuzzer until a memory error occurs
./fuzz/fuzz-magnet -max_len=2000 -only_ascii=1

# Run two parallel jobs for the magnet URI fuzzer
./fuzz/fuzz-magnet -max_len=2000 -only_ascii=1 -rss_limit_mb=8192 -jobs=2
```
