# `results/sample_run/`: SYNTHETIC DATA, not a real hardware measurement

**Read this before citing anything from `report/main.pdf` in a resume,
interview, or anywhere else.**

The two CSVs in this directory (`results.csv`, `pingpong.csv`) were produced
by `generate_synthetic_sample.py`, not by running `apps/benchmark` or
`apps/pingpong` on real hardware. They exist so `analysis/run_full_analysis.py`
and the LaTeX report have something concrete to run against end to end.

## Why

This repository's C++/MPI code, test suite, and benchmark harness were
written in a sandbox with **no MPI installation and no network access to
install one** (see `docs/DESIGN_DECISIONS.md`). Every line of C++ was
syntax- and type-checked against a stand-in `mpi.h`, and the ring
algorithm's index arithmetic was validated for real via
`tests/validate_ring_logic.py` (a transport-independent simulation), but no
process in that sandbox ever actually called `MPI_Isend`/`MPI_Irecv` for
real, and no wall-clock timing measurement in this directory reflects actual
network, memory-bandwidth, or scheduler behavior on any machine.

## What the numbers actually are

`generate_synthetic_sample.py` computes a mean time from the Hockney cost
model, `T = 2(N-1)*alpha + 2*(N-1)/N*bytes*beta`, for plausible,
order-of-magnitude `(alpha, beta)` constants per algorithm, then draws 20
lognormal-perturbed samples around that mean per configuration (matching
`apps/benchmark.cpp`'s CSV schema exactly: one row per `{algorithm,
num_procs, dtype, message_size_bytes, time_stat}`). The `(alpha, beta)`
constants are not fitted to, derived from, or claimed to represent any real
machine, including the i7-14700K / 32GB DDR5 workstation this project's
build spec names as the intended target hardware.

## What to do instead

```bash
cmake -S . -B build -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure     # must be green first
scripts/run_local_sweep.sh                      # or run_slurm_sweep.sbatch on a cluster
python3 analysis/run_full_analysis.py --results results/local_run/results.csv \
    --pingpong results/local_run/pingpong.csv --outdir report
```

`scripts/run_local_sweep.sh` writes to `results/local_run/` by default
(gitignored, see `.gitignore`), never back into this directory, so real and
synthetic data can't accidentally merge. Point `analysis/run_full_analysis.py`
at the real CSVs and every figure, table, and fitted alpha/beta in
`report/main.pdf` will regenerate from genuine measurements.
