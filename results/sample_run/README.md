# `results/sample_run/`: real single-node Microsoft MPI measurements

The two CSVs in this directory (`results.csv`, `pingpong.csv`) are real
measurements, produced by running `apps/benchmark` and `apps/pingpong` under
a working Microsoft MPI installation on the workstation described in
`docs/BENCHMARKING.md`. They are the dataset `analysis/run_full_analysis.py`
and the LaTeX report build against by default, and every figure and table in
`report/main.pdf` is derived from them.

## Provenance

The run was collected on an Intel Core i7-14700K (32 GB DDR5) under the
Microsoft MPI 10.1 runtime, with the MSYS2 UCRT64 `mpicxx` wrapper (GCC 15.2)
providing the MPI development headers and link library. It was gathered with
`scripts/run_local_sweep.ps1 -Quick`, which launches one `mpiexec` per
process count and lets `apps/benchmark` sweep every message size and
algorithm inside that launch.

Correctness was established before any timing was trusted: the full `ctest`
matrix (`N` in {1, 2, 3, 4, 5, 7, 8, 16}, including non-power-of-two counts
and zero-size-chunk cases) passes against this same real MPI, and each
configuration's ring output is checked against both vendor `MPI_Allreduce`
and an independently computed reference inside the benchmark before its time
is recorded.

## Scope and honest caveats

- **Single node.** All ranks share one machine, so the transport is
  Microsoft MPI's shared-memory path, not a network fabric. The fitted
  `alpha`/`beta` describe that path. A visible consequence: bus bandwidth
  peaks in the L3-cache-resident size range (hundreds of KiB to low MiB)
  and falls off for larger, memory-bound messages, so a single-slope
  Hockney fit only loosely describes the data (R^2 around 0.3 to 0.4).
  `report/main.pdf` discusses this directly rather than hiding it.
- **Two algorithm variants, not three.** Microsoft MPI has no equivalent of
  Open MPI's `coll/tuned` MCA controls, so the forced vendor-ring
  (`mpi-ring`) variant cannot be produced here; the data compares the custom
  ring against the vendor's default `MPI_Allreduce` selection only.
- **`--quick` process-count grid.** `N` in {2, 4, 8, 16} rather than the full
  2..16, to keep the sweep's wall-clock time reasonable on a shared desktop.
  The message-size axis is the full 8 B to 128 MiB (25 powers of two).

## Regenerating or extending this dataset

```bash
# Unix / Open MPI (also enables the forced mpi-ring variant and full N=2..16):
cmake -S . -B build -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure     # must be green first
scripts/run_local_sweep.sh                      # or run_slurm_sweep.sbatch on a cluster
python3 analysis/run_full_analysis.py --results results/local_run/results.csv \
    --pingpong results/local_run/pingpong.csv --outdir report
```

```powershell
# Windows / Microsoft MPI (this machine):
.\scripts\run_local_sweep.ps1 -Quick -BuildDir build-real
py analysis\run_full_analysis.py --results results\local_run\results.csv `
    --pingpong results\local_run\pingpong.csv --outdir report
```

The sweep scripts write to `results/local_run/` (gitignored), never back
into this directory. `generate_synthetic_sample.py` in this folder is not
the source of these CSVs: it is retained only as the harness that produces a
synthetic dataset with known `alpha`/`beta`, used to validate the
weighted-least-squares fitting procedure in `analysis/theoretical_model.py`
(see `docs/DESIGN_DECISIONS.md`).
