# `results/sample_run/`: real Open MPI measurements

The two CSVs here (`results.csv`, `pingpong.csv`) are real measurements,
produced by running `apps/benchmark` and `apps/pingpong` against a working
Open MPI installation. They are the dataset `analysis/run_full_analysis.py`
and the LaTeX report build against by default, and every figure and table in
`report/main.pdf` derives from them. Nothing here is modeled or synthetic.

## Provenance

- **MPI:** Open MPI 5.0.10, `coll_tuned_allreduce_algorithm` ring id = 4
  (read from the installed library, not assumed; see `docs/BENCHMARKING.md`).
- **Hardware:** Intel Core i7-14700K, 32 GB DDR5, 28 hardware threads.
- **OS/toolchain:** Ubuntu on WSL2, GCC 15.2, CMake, warnings as errors.
- **Date:** 2026-07-12.
- **Command:** `scripts/run_local_sweep.sh` (full grid, no `--quick`).

## Coverage

- **3 algorithm variants:** `ring` (this project's from-scratch
  implementation), `mpi-default` (Open MPI's own `coll/tuned` selection), and
  `mpi-ring` (`MPI_Allreduce` forced to Open MPI's ring via
  `--mca coll_tuned_use_dynamic_rules 1 --mca coll_tuned_allreduce_algorithm 4`).
  The third is what makes the ring-versus-ring comparison possible, isolating
  implementation quality from algorithm choice.
- **Process counts:** every N from 2 to 16 inclusive, including the
  non-power-of-two counts. With 28 hardware threads and at most 16 ranks,
  nothing is oversubscribed.
- **Message sizes:** 25 powers of two, 8 B through 128 MiB.
- 5625 result rows (3 x 15 x 25 x 5 stats) and 125 pingpong rows.

Correctness was established before any timing was trusted: the full `ctest`
matrix (N in {1,2,3,4,5,7,8,16}, including zero-size-chunk cases) passes
against this same Open MPI, and each configuration's ring output is checked
against both vendor `MPI_Allreduce` and an independently computed reference
inside the benchmark before its time is recorded.

## The one caveat: single node

All ranks share one machine, so the transport is Open MPI's shared-memory
path, not a network fabric. The visible consequence is that bus bandwidth
peaks in the L3-cache-resident size range (hundreds of KiB to low MiB, about
16 GB/s here) and falls off for larger, memory-bound messages, so a
single-slope Hockney fit only loosely describes the data (R^2 about 0.28 to
0.33). The report discusses this directly rather than hiding it. A
multi-node sweep over a real fabric (`scripts/run_slurm_sweep.sbatch`) would
be expected to track the linear model far more closely, and is the most
valuable extension to this work.

## Regenerating

```bash
cmake -S . -B build -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure     # must be green first
scripts/check_mpi_algorithms.sh                # confirm YOUR ring algo id
scripts/run_local_sweep.sh                     # or --quick for N in {2,4,8,16}
python3 analysis/run_full_analysis.py \
    --results results/local_run/results.csv \
    --pingpong results/local_run/pingpong.csv --outdir report
```

The sweep scripts write to `results/local_run/` (gitignored), never back into
this directory. This directory contains real measurements and nothing else:
there is no synthetic data anywhere in it, and no script here that could
overwrite the CSVs.

(The fitting procedure in `analysis/theoretical_model.py` is separately
validated against data synthesized from known constants, since proving a
fitter recovers the right answer requires knowing that answer in advance.
That check lives in `analysis/validate_fit.py`, writes no files, and never
touches this directory. See `docs/DESIGN_DECISIONS.md`.)
