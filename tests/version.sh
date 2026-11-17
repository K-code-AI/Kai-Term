#!/bin/sh
# Minimal M0 smoke test: the binary must report its version.
set -eu
BIN="${1:-build/kai-term}"
"$BIN" --version
