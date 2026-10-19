#!/usr/bin/env bash
set -euo pipefail

export RUSTUP_HOME="/tmp/rustup"
export CARGO_HOME="/tmp/cargo"

sh /tmp/rustup-init.sh -y --profile minimal --no-modify-path
/tmp/cargo/bin/rustup target add x86_64-pc-windows-gnu
/tmp/cargo/bin/rustc --version
/tmp/cargo/bin/cargo --version
