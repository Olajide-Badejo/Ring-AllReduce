#!/usr/bin/env bash
set -euo pipefail

# Runs the full local benchmark sweep: process counts 2..16 (or {2,4,8,16}
# with --quick), each launched once for {ring, mpi-default} and once more
# with Open MPI's tuned collective forced to the ring algorithm for
# {mpi-ring}. These need separate mpirun invocations because MCA parameters
# are process-environment state read at MPI_Init, not something the
# benchmark binary can flip mid-run (see apps/benchmark.cpp's file header
# and docs/BENCHMARKING.md). Also runs the 2-rank raw pingpong latency
# reference once.
#
# IMPORTANT: run scripts/check_mpi_algorithms.sh first and confirm the ring
# algorithm's ID for your installed Open MPI version -- it has moved across
# releases and 4 is a common default, not a guarantee. Override with
# RING_ALGO_ID=<id> ./scripts/run_local_sweep.sh if yours differs.
#
# Usage:
#   ./scripts/run_local_sweep.sh            # full sweep, N=2..16
#   ./scripts/run_local_sweep.sh --quick     # N in {2,4,8,16} only
#   RING_ALGO_ID=3 DTYPE=double ./scripts/run_local_sweep.sh

RING_ALGO_ID="${RING_ALGO_ID:-4}"
BUILD_DIR="${BUILD_DIR:-build}"
OUTPUT_DIR="${OUTPUT_DIR:-results/local_run}"
DTYPE="${DTYPE:-float}"

QUICK=0
for arg in "$@"; do
  case "$arg" in
    --quick) QUICK=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 1 ;;
  esac
done

if [ "$QUICK" -eq 1 ]; then
  PROC_COUNTS=(2 4 8 16)
else
  PROC_COUNTS=(2 3 4 5 6 7 8 9 10 11 12 13 14 15 16)
fi

BENCH="$BUILD_DIR/apps/benchmark"
PINGPONG="$BUILD_DIR/apps/pingpong"
if [ ! -x "$BENCH" ] || [ ! -x "$PINGPONG" ]; then
  echo "error: $BENCH / $PINGPONG not found or not executable." >&2
  echo "Build the project first: cmake -S . -B $BUILD_DIR && cmake --build $BUILD_DIR -j" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"
RESULTS_CSV="$OUTPUT_DIR/results.csv"
PINGPONG_CSV="$OUTPUT_DIR/pingpong.csv"
# Both binaries APPEND to their CSV, so stale output from a previous sweep
# must be cleared or the new rows are silently concatenated onto the old
# ones (which quietly mixes two different machines' or MPI stacks' numbers
# into one file). Remove both, not just the results file.
rm -f "$RESULTS_CSV" "$PINGPONG_CSV"

echo "== ring-allreduce local sweep: N in {${PROC_COUNTS[*]}}, dtype=$DTYPE, ring algo id=$RING_ALGO_ID =="
echo "== output: $RESULTS_CSV =="

for N in "${PROC_COUNTS[@]}"; do
  echo "--- N=$N: ring, mpi-default ---"
  mpirun --allow-run-as-root --oversubscribe -np "$N" "$BENCH" \
    --algorithm ring,mpi-default --dtype "$DTYPE" --output "$RESULTS_CSV"

  echo "--- N=$N: mpi-ring (forced algorithm id=$RING_ALGO_ID) ---"
  mpirun --allow-run-as-root --oversubscribe \
    --mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_allreduce_algorithm "$RING_ALGO_ID" \
    -np "$N" "$BENCH" --algorithm mpi-ring --dtype "$DTYPE" --output "$RESULTS_CSV"
done

echo "--- pingpong (N=2) ---"
mpirun --allow-run-as-root --oversubscribe -np 2 "$PINGPONG" --output "$PINGPONG_CSV"

echo
echo "Done."
echo "  $RESULTS_CSV"
echo "  $PINGPONG_CSV"
echo "Next: python3 analysis/run_full_analysis.py --results $RESULTS_CSV --pingpong $PINGPONG_CSV --outdir report"
