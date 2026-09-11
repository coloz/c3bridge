#!/bin/sh
set -eu
cd "$(dirname "$0")"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
g++ -std=c++11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Ihci_fakes -I../src hci_test.cpp ../src/ESPLinkHCI.cpp \
  -o "$out/hci_test"
"$out/hci_test"
