#!/usr/bin/env bash
set -euo pipefail

# First run is labelled first; repeats reuse the corpus without flushing OS caches.
# Usage: benchmark.sh BIN CORPUS_ROOT CSV "FILE_COUNTS" "WORD_COUNTS" "THREAD_COUNTS" RUNS QUERIES
BIN="${1:-./build/loomsearch}"
CORPUS="${2:-./out/benchmark-corpora}"
OUTPUT="${3:-./out/benchmark.csv}"
read -r -a FILE_COUNTS <<< "${4:-200 2000}"
read -r -a WORD_COUNTS <<< "${5:-20 200}"
read -r -a THREAD_COUNTS <<< "${6:-1 4}"
RUNS="${7:-3}"
QUERIES="${8:-30}"

[[ -x "$BIN" && ! -e "$OUTPUT" ]] || { echo 'Missing binary or output already exists' >&2; exit 1; }
for value in "$RUNS" "$QUERIES" "${FILE_COUNTS[@]}" "${WORD_COUNTS[@]}" "${THREAD_COUNTS[@]}"; do
    [[ "$value" =~ ^[0-9]+$ && "$value" -gt 0 ]] || { echo 'Matrix values must be positive integers' >&2; exit 1; }
done
(( RUNS >= 2 )) || { echo 'RUNS must be >= 2' >&2; exit 1; }
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
mkdir -p "$CORPUS" "$(dirname "$OUTPUT")"
CORPUS="$(cd "$CORPUS" && pwd)"
OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
scratch="$(mktemp -d)"
trap 'rm -r -- "$scratch"' EXIT

measure() {
    local name="$1"; shift
    if [[ "$(uname)" == Darwin ]]; then
        /usr/bin/time -l -o "$scratch/$name.time" "$BIN" "$@" > "$scratch/$name.out"
        awk '/maximum resident set size/ { print $1 }' "$scratch/$name.time" > "$scratch/$name.peak"
    else
        /usr/bin/time -f '%M' -o "$scratch/$name.time" "$BIN" "$@" > "$scratch/$name.out"
        awk '{ print $1 * 1024 }' "$scratch/$name.time" > "$scratch/$name.peak"
    fi
}

metric() { awk -F ': ' -v key="$1" '$1 == key { print $2; exit }' "$scratch/bench.out"; }
printf 'files,words_per_file,threads,run,cache_state,hand_peak_working_set_bytes,sqlite_peak_working_set_bytes,hand_build_ms,sqlite_build_ms,hand_single_p50_ns,hand_single_p95_ns,sqlite_single_p50_ns,sqlite_single_p95_ns,hand_and_p50_ns,hand_and_p95_ns,sqlite_and_p50_ns,sqlite_and_p95_ns,parity_mismatches\n' > "$OUTPUT"
for files in "${FILE_COUNTS[@]}"; do
    for words in "${WORD_COUNTS[@]}"; do
        case_dir="$CORPUS/files_${files}_words_${words}"
        mkdir -p "$case_dir"
        "$BIN" corpus-gen "$case_dir" --files "$files" --words "$words" > /dev/null
        actual="$(find "$case_dir" -maxdepth 1 -type f | wc -l | tr -d ' ')"
        [[ "$actual" == "$files" ]] || { echo "Unexpected files in $case_dir" >&2; exit 1; }
        for threads in "${THREAD_COUNTS[@]}"; do
            for ((run=1; run<=RUNS; run++)); do
                state=repeat
                (( run == 1 )) && state=first
                measure hand index "$case_dir" --threads "$threads" --output "$scratch/index.bin"
                measure sqlite sqlite-index "$case_dir" --threads "$threads" --output "$scratch/index.db"
                measure bench bench-search "$case_dir" --threads "$threads" --queries "$QUERIES"
                [[ "$(metric parity_mismatches)" == 0 && "$(metric corpus_files)" == "$files" ]] || {
                    echo "Parity or corpus count failed: $case_dir" >&2; exit 1;
                }
                for key in hand_index_build_ms sqlite_index_build_ms hand_single_p50_ns hand_single_p95_ns sqlite_single_p50_ns sqlite_single_p95_ns hand_and_p50_ns hand_and_p95_ns sqlite_and_p50_ns sqlite_and_p95_ns; do
                    [[ -n "$(metric "$key")" ]] || { echo "Missing benchmark metric: $key" >&2; exit 1; }
                done
                printf '%s\n' "$files,$words,$threads,$run,$state,$(cat "$scratch/hand.peak"),$(cat "$scratch/sqlite.peak"),$(metric hand_index_build_ms),$(metric sqlite_index_build_ms),$(metric hand_single_p50_ns),$(metric hand_single_p95_ns),$(metric sqlite_single_p50_ns),$(metric sqlite_single_p95_ns),$(metric hand_and_p50_ns),$(metric hand_and_p95_ns),$(metric sqlite_and_p50_ns),$(metric sqlite_and_p95_ns),$(metric parity_mismatches)" >> "$OUTPUT"
                echo "files=$files words=$words threads=$threads run=$run"
            done
        done
    done
done
