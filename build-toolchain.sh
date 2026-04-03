#!/usr/bin/env bash
# Compile everything under src/ into bin/ (this demo’s bin is only those artifacts; bin_repo ignored).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin
g++ -std=c++17 -O2 -Wall -Wextra -o bin/wlp4scan src/wlp4scan.cc
g++ -std=c++17 -O2 -Wall -Wextra -o bin/wlp4parse src/wlp4parse.cc
g++ -std=c++17 -O2 -Wall -Wextra -o bin/wlp4type src/wlp4type.cc
g++ -std=c++17 -O2 -Wall -Wextra -o bin/wlp4gen src/wlp4gen.cc
echo "OK: bin/wlp4scan bin/wlp4parse bin/wlp4type bin/wlp4gen. See docs/bin.txt."
