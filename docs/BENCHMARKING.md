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
`results/sample_run/` dataset instead, which is synthetic (see that
directory's own README.md) and will compile in well under a minute since it
involves no MPI at all.

## This machine's Open MPI version and ring algorithm ID

**Not yet filled in.** This repository was built in a sandbox with no MPI
installation and no network access to install one (see
`docs/DESIGN_DECISIONS.md`), so `scripts/check_mpi_algorithms.sh` has never
actually been run against a real Open MPI build. Whoever runs the first
real sweep should replace this section with:

```
Open MPI version: <output of `ompi_info --version`>
coll_tuned_allreduce_algorithm ring id: <id, from scripts/check_mpi_algorithms.sh>
Date of sweep: <date>
Hardware: <CPU, RAM, core count actually used -- see below>
```

Algorithm IDs have moved across Open MPI releases; do not reuse a number
from a different machine's run without re-checking it.

## Target and actually-used hardware

This project's build specification names an Intel Core i7-14700K
workstation (32 GB DDR5, an RTX 5070 that this CPU-only project does not
use) as the intended benchmarking machine. The pipeline itself
(`analysis/`, the LaTeX report build) was developed and exercised in a
sandboxed Linux container with a single virtualized CPU core and roughly
3.9 GB of RAM, which has no bearing on real ring-allreduce performance and
is mentioned only because Section "A note on the dataset behind this
report" in `report/sections/04_experimental_setup.tex` (and this file's
section above) both depend on that distinction being clear: the pipeline
was validated there; no benchmark numbers were ever measured there.

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
