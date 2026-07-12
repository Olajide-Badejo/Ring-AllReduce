# Benchmarking

## Reproducing a real sweep

```bash
# 1. Build, with warnings as errors and the test suite enabled.
cmake -S . -B build -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
cmake --build build -j

# 2. Correctness first. Must be green before any benchmark number means anything.
ctest --test-dir build --output-on-failure

# 3. Check this machine's Open MPI version and ring algorithm ID.
scripts/check_mpi_algorithms.sh
# If the ring algorithm ID printed differs from this repo's default (4):
export RING_ALGO_ID=<id>

# 4. Run the sweep. --quick restricts N to {2,4,8,16} for fast iteration;
#    omit it for the full N=2..16 sweep.
scripts/run_local_sweep.sh --quick

# 5. Regenerate the report from what you just measured.
python3 analysis/run_full_analysis.py \
    --results results/local_run/results.csv \
    --pingpong results/local_run/pingpong.csv \
    --outdir report
cd report && latexmk -pdf main.tex
```

Or, from a clean checkout, `make bench` (opt-in, runs a real sweep) followed
by `make report` (regenerates figures/tables from whatever
`results/local_run/` currently holds and compiles the PDF). `make report`
alone, with no prior `make bench`, uses the committed
`results/sample_run/` dataset instead, which is a real single-node Open MPI
measurement (see that directory's own README.md for provenance and the
single-node caveats) and will compile in well under a minute since the
report build itself involves no MPI at all.

On Windows, `make` needs an explicit interpreter for the analysis step
(`make PYTHON=py report`), because the bare `python3` on PATH is often the
MSYS/UCRT build with no pip or numpy.

## This machine's Open MPI version and ring algorithm ID

These are the values the committed `results/sample_run/` dataset was actually
measured against, read from the installed library rather than assumed:

```
Open MPI version: 5.0.10
coll_tuned_allreduce_algorithm ring id: 4
Date of sweep: 2026-07-12
Hardware: Intel Core i7-14700K, 32 GB DDR5, 28 hardware threads
OS/toolchain: Ubuntu on WSL2, GCC 15.2, CMake
```

Full `coll_tuned_allreduce_algorithm` enumerator on Open MPI 5.0.10, from
`scripts/check_mpi_algorithms.sh`:

```
0:ignore  1:basic_linear  2:nonoverlapping  3:recursive_doubling
4:ring    5:segmented_ring  6:rabenseifner
```

So the forced vendor-ring comparison uses:

```bash
mpirun --mca coll_tuned_use_dynamic_rules 1 \
       --mca coll_tuned_allreduce_algorithm 4 -np N ./build/apps/benchmark ...
```

Algorithm IDs have moved across Open MPI releases; do not reuse a number
from a different machine's run without re-checking it with
`scripts/check_mpi_algorithms.sh`. Note that the enumerator lists
`recursive_doubling` and `rabenseifner` as separate selectable algorithms:
those are the O(log N)-step algorithms the report argues the default
selection prefers over a ring at small message sizes.

## Running on Windows

There is no native Open MPI for Windows, and Microsoft MPI has no
`coll/tuned` MCA controls, so it cannot produce the forced `mpi-ring`
variant. Use WSL2 (which is how the committed dataset was measured on this
workstation):

```bash
sudo apt-get update
sudo apt-get install -y openmpi-bin libopenmpi-dev cmake build-essential
```

Then follow "Reproducing a real sweep" above inside WSL. Build on the WSL
native filesystem rather than under `/mnt/c`: CMake's `configure_file` fails
with "Operation not permitted" on the Windows drive mount. A `.gitattributes`
rule keeps `*.sh` at LF endings so the sweep scripts do not break under bash
with `set: pipefail: invalid option name` when checked out on Windows.

## Target and actually-used hardware

This project's build specification names an Intel Core i7-14700K
workstation (32 GB DDR5, an RTX 5070 that this CPU-only project does not
use) as the intended benchmarking machine, and that is the machine the
committed dataset was measured on. The `results/sample_run/` data is a real
full sweep: all three algorithm variants (`ring`, `mpi-default`, forced
`mpi-ring`), every process count N = 2..16 including the non-power-of-two
ones, across the full 8 B to 128 MiB message-size axis, plus a matching
`pingpong` latency reference. With 28 hardware threads and a maximum of 16
ranks, nothing is oversubscribed.

The one scoping caveat is that it is single-node, so the transport is Open
MPI's shared memory rather than a network fabric. See
`results/sample_run/README.md` and the report's Section 4 for what that
implies (a cache-resident bus-bandwidth peak, and a loose fit of the linear
cost model).

## Statistical methodology

Each `(algorithm, N, dtype, message_size)` configuration runs a fixed
number of untimed warm-up iterations, then a variable number of timed
repetitions bounded by both a minimum rep count and a wall-clock budget
(see `apps/benchmark.cpp`'s adaptive loop and `docs/DESIGN_DECISIONS.md`
for why a single fixed rep count does not work well across a sweep spanning
8 bytes to 128 MiB). Every repetition is timed with `MPI_Wtime` on every
rank and reduced to rank 0 via `MPI_Reduce(..., MPI_MAX, ...)`, since an
allreduce completes only once its slowest participating rank finishes.
Min, mean, median, max, and standard deviation are all recorded; this
project's analysis pipeline uses the **median** as "achieved" performance
throughout (see `docs/DESIGN_DECISIONS.md` for why, over the more common
alternative of using the minimum).

## Known gotchas

- Running as root (common in CI containers) requires
  `--allow-run-as-root` on every `mpirun` invocation, or Open MPI refuses
  to launch.
- Launching more ranks than available cores requires `--oversubscribe`,
  or Open MPI refuses to launch. Both flags are harmless when not
  strictly needed (not root; N within available cores), so
  `scripts/run_local_sweep.sh` and `tests/CMakeLists.txt`'s `add_test`
  calls both pass them unconditionally rather than trying to detect when
  they are required.
- `coll_tuned_allreduce_algorithm`'s numeric IDs are not stable across
  Open MPI releases. Always run `scripts/check_mpi_algorithms.sh` on the
  machine you are about to benchmark, not once and never again.
