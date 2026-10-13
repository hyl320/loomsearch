# Phase 1 benchmark (2026-10-13)

## Setup

- Baseline: `ca043b7` (before phase 1); candidate: `ca043b7` plus uncommitted phase-1 changes. The baseline was built in a detached worktree at `D:\loomlog-baseline-ca043b7`; no source changes were made there.
- Windows 10.0.26200, Intel Core i7-12650H (10 cores / 16 logical processors), MSYS2 GCC 16.2.0, Ninja, CMake Release, tests and standalone benches disabled for both builds.
- `corpus-gen` produced fixed corpora of 2,000 files x 80 words and 6,000 files x 200 words. Each version read the same corpus. For each corpus and thread count (1, 4), run 1-3 alternated baseline then candidate. Every run used fresh CLI processes; OS file caches were **not** cleared. Results below are the median of three runs, not statistically independent cold-cache measurements.
- Peak memory: Windows `Process.PeakWorkingSet64`, measured separately for `index` and `sqlite-index` processes, in MiB. Build times: `bench-search` output (`hand_index_build_ms` includes saving the binary index; `sqlite_index_build_ms` includes opening the database and committing). The peak-memory and build-time figures come from separate process invocations, not the same run.
- Query set: `bench-search --queries 30`. All 24 baseline/candidate runs reported `parity_mismatches: 0`. Baseline query timings cover single words only. Candidate also ran adjacent high-frequency-word AND queries. Query terms tied by posting-list length are not guaranteed to have identical ordering across builds, so query totals are exploratory.

## Results

Median of three runs. Each cell is **baseline -> candidate**.

| Files x words | Threads | Hand build ms | Hand peak MiB | FTS5 build ms | FTS5 peak MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2,000 x 80 | 1 | 268 -> 278 | 50.3 -> 50.3 | 197 -> 209 | 13.3 -> 12.6 |
| 2,000 x 80 | 4 | 146 -> 144 | 50.8 -> 50.4 | 92 -> 126 | 13.6 -> 12.8 |
| 6,000 x 200 | 1 | 1,080 -> 1,216 | 252.3 -> 250.8 | 798 -> 866 | 22.9 -> 16.9 |
| 6,000 x 200 | 4 | 846 -> 872 | 254.7 -> 252.3 | 598 -> 629 | 23.8 -> 17.4 |

Median of three single-word query totals, microseconds for 30 terms (baseline -> candidate):

| Files x words | Threads | Hand query total us | FTS5 query total us |
| --- | ---: | ---: | ---: |
| 2,000 x 80 | 1 | 6,200 -> 5,700 | 47,050 -> 43,239 |
| 2,000 x 80 | 4 | 6,034 -> 5,879 | 46,435 -> 44,960 |
| 6,000 x 200 | 1 | 42,273 -> 41,144 | 1,071,556 -> 848,465 |
| 6,000 x 200 | 4 | 35,334 -> 32,403 | 820,739 -> 746,787 |

## Interpretation and limits

- At 6,000 x 200, bounding file-read tasks reduces FTS5 peak working set by about 6 MiB (roughly 26-27%). The hand-built index improves by only about 1.5-2.4 MiB (<1%): the resident inverted index dominates this measurement, not the task queue. Larger files or many more files may expose a different task-queue contribution.
- Build throughput is not uniformly better. FTS5 build time regresses in three of four median comparisons and the hand-built backend regresses noticeably at 6,000 files with one thread. The statement cache and bounded scheduling changed together; these measurements cannot isolate either contribution. No additional concurrency machinery is justified by these results alone.
- The old `bench-search` did not execute AND queries or report p50/p95. New AND-query percentiles are available in the candidate CLI, but there is **no valid before/after latency measurement** for the posting-list ordering change. Single-word totals cannot establish its benefit. Existing single-word timings are also sensitive to term selection and cache state; do not treat the differences above as a speedup claim.
- `first`/`repeat` in `scripts/benchmark.ps1` and `scripts/benchmark.sh` mean first and subsequent processes against the same corpus, respectively. They are not strict cold/hot OS cache states. Neither run models incremental indexing; that belongs to the next phase.

## Reproduction

Build each source tree with `cmake -S <source> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DLOOMLOG_BUILD_TESTS=OFF -DLOOMLOG_BUILD_BENCH=OFF`, then `cmake --build <build> --target loomsearch -j 4`. Generate the two corpora with `corpus-gen --files 2000 --words 80` and `corpus-gen --files 6000 --words 200` in separate directories. Run `index`, `sqlite-index`, and `bench-search --queries 30` per version, thread count, and repetition as specified above; compare each pair's medians. The matrix scripts provide reusable candidate-side CSV output, but the baseline predates their AND-query fields and must not be presented as having those measurements.
