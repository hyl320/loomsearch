#!/usr/bin/env bash
set -euo pipefail

# LoomSearch 一键 benchmark：固定语料 + 固定参数，结果可复现。
# 用法: ./scripts/benchmark.sh [loomsearch路径] [语料目录] [files] [words] [threads] [queries]
BIN="${1:-./build/loomsearch}"
CORPUS="${2:-./out/corpus}"
FILES="${3:-2000}"
WORDS="${4:-80}"
THREADS="${5:-4}"
QUERIES="${6:-30}"

"$BIN" corpus-gen "$CORPUS" --files "$FILES" --words "$WORDS"
"$BIN" bench-search "$CORPUS" --threads "$THREADS" --queries "$QUERIES"
