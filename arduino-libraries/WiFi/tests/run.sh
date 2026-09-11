#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
repo=$(pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
for variant in ZONED_IPV6 IPV4_ONLY IPV6_NO_ZONE; do
  g++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -D"WIFI_TEST_$variant" \
    -I"$repo/arduino-libraries/WiFi/tests/fakes" -I"$repo/arduino-libraries/WiFi/src" -I"$repo/arduino-libraries/ESPLink/src" \
    "$repo/arduino-libraries/WiFi/tests/network_semantics.cpp" "$repo"/arduino-libraries/WiFi/src/*.cpp -o "$out/network_semantics"
  printf '%s: ' "$variant"
  "$out/network_semantics"
done
g++ -std=c++11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I"$repo/arduino-libraries/WiFi/src" \
  "$repo/arduino-libraries/WiFi/tests/array_test.cpp" -o "$out/array_test"
"$out/array_test"
