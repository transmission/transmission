#!/usr/bin/env sh

./run-clang-format.py -i -r cli daemon gtk libtransmission qt tests utils
cd web && yarn -s install && yarn -s lint:fix
