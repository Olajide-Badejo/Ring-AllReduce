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

## Windows desktop using Microsoft MPI

This local desktop has a real Microsoft MPI Runtime 10.1 installation and
MSYS2 UCRT64's `mingw-w64-ucrt-x86_64-msmpi` development package. The
following commands build and test against that real MPI implementation:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build-real -G "MinGW Makefiles" `
  -DMPI_CXX_COMPILER=C:\msys64\ucrt64\bin\mpicxx.exe `
  -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
cmake --build build-real -j 8
ctest --test-dir build-real --output-on-failure
```

Run a real local sweep with the native launcher wrapper:

```powershell
.\scripts\run_local_sweep.ps1 -Quick -BuildDir build-real
python analysis\run_full_analysis.py `
  --results results\local_run\results.csv `
  --pingpong results\local_run\pingpong.csv `
  --outdir report
```

`run_local_sweep.ps1` measures the custom ring and Microsoft MPI's default
`MPI_Allreduce` selection. Microsoft MPI has no Open MPI `coll/tuned` MCA
ring selector, so it deliberately does not emit `mpi-ring` rows. Use the
Open MPI workflow below when the forced vendor-ring comparison is required.

## This machine's Open MPI version and ring algorithm ID

**Not yet filled in.** The local Windows verification uses Microsoft MPI,
not Open MPI, so `scripts/check_mpi_algorithms.sh` has not run against an
Open MPI build here. Whoever runs the first Open MPI sweep should replace
this section with:

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
use) as the intended benchmarking machine. The real Microsoft MPI smoke
measurements described above were collected locally on this desktop, not in
a sandbox. They cover only N = 2 and 8 B through 64 KiB, so they validate
the execution path but are not a replacement for the full benchmark grid.

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
