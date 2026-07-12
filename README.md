[![CI](../../actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

# ring-allreduce

A from-scratch implementation of the ring-allreduce collective, built
directly on MPI point-to-point primitives, with no MPI collective call
anywhere inside it, benchmarked against vendor `MPI_Allreduce` and analyzed
against a fitted latency-bandwidth cost model. This repository backs three
specific claims:

> Designed and implemented a ring-allreduce collective from scratch in C++
> using MPI point-to-point communication, mirroring the core algorithm used
> in GPU communication libraries such as NCCL.

The algorithm itself is `include/ring_allreduce/ring_allreduce.hpp`, two
phases of `MPI_Isend`/`MPI_Irecv`/`MPI_Waitall` and nothing else; see
`docs/ALGORITHM.md` for the full index-arithmetic derivation and
`report/sections/02_background.tex` for the same material with a diagram.

> Benchmarked the custom implementation against `MPI_Allreduce` across
> message sizes (8B-128MB) and process counts (2-16), evaluating bus
> bandwidth efficiency and algorithmic step count.

`apps/benchmark.cpp` sweeps exactly that range against both Open MPI's
default algorithm selection and `MPI_Allreduce` forced to its own ring
algorithm; `report/sections/05_results.tex` and Table 1 in
`report/sections/02_background.tex` are where the efficiency and step-count
numbers actually land.

> Analyzed the gap between theoretical and achieved bus bandwidth using
> Python; identified pipeline fill latency and rank synchronization
> overhead as primary bottlenecks at small message sizes.

`analysis/theoretical_model.py` fits the Hockney model's alpha and beta
from measured data (weighted least squares, not an assumed constant; see
`docs/DESIGN_DECISIONS.md` for why that weighting mattered), and
`report/sections/06_discussion.tex` is where the two bottlenecks get
separated and each one quantified, not just asserted.

**About the committed numbers:** the dataset at `results/sample_run/` is a
**real measurement**, collected on an i7-14700K under Microsoft MPI (see
`results/sample_run/README.md` for full provenance). Every figure and table
in `report/main.pdf` is derived from it. Two honest caveats come with the
environment: it is a single-node run (shared-memory transport, so bus
bandwidth peaks in the L3-cache-resident size range and a single-slope
Hockney fit only loosely describes it), and Microsoft MPI cannot force a
vendor ring, so the data compares the custom ring against the vendor's
default `MPI_Allreduce` only. The headline result is a clean crossover: the
ring loses to the vendor default below a few kilobytes (more latency-bound
steps) and wins above it by up to a factor of two (bandwidth optimality).
Running the Open MPI sweep on a cluster followed by `make report` adds the
forced ring-versus-ring variant and the full 2..16 process-count grid using
the identical methodology.

![Achieved bus bandwidth vs message size](docs/assets/busbw_preview.png)
*Real single-node Microsoft MPI measurements, rendered by
`analysis/generate_plots.py`. The README preview refreshes automatically as
part of `make report`.*

## Quickstart

```bash
git clone <this repo>
cd ring-allreduce

# Build and run the correctness suite (needs Open MPI or MPICH + CMake >= 3.16).
cmake -S . -B build -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# Hand-runnable sanity check.
mpirun --allow-run-as-root --oversubscribe -np 8 ./build/apps/correctness_check

# Regenerate the report from the committed sample dataset (no MPI needed
# for this part, just Python + a TeX distribution): report/main.pdf in a
# few seconds.
make report
```

To run your own benchmark sweep and rebuild the report from it instead of
the committed sample, see `docs/BENCHMARKING.md`; the short version is
`make bench` (opt-in, actually launches `mpirun` repeatedly) followed by
`make report` again. On an Open MPI cluster this also adds the forced
ring-versus-ring variant that a single-node Microsoft MPI run cannot
produce.

## Windows desktop with Microsoft MPI

The project has been built and tested locally with the Microsoft MPI runtime
and the MSYS2 UCRT64 MPI development package. This route runs the custom
ring and Microsoft MPI's default `MPI_Allreduce`; it cannot force Open MPI's
vendor ring algorithm, so the `mpi-ring` comparison remains specific to the
Open MPI workflow.

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build-real -G "MinGW Makefiles" `
  -DMPI_CXX_COMPILER=C:\msys64\ucrt64\bin\mpicxx.exe `
  -DRING_ALLREDUCE_WARNINGS_AS_ERRORS=ON
cmake --build build-real -j 8
ctest --test-dir build-real --output-on-failure
.\scripts\run_local_sweep.ps1 -Quick -BuildDir build-real
```

See `docs/BENCHMARKING.md` for the full Windows command and the separate
Open MPI workflow.

## Directory overview

```
include/ring_allreduce/  the algorithm: chunking, reduce ops, timer, ring_allreduce
src/                     implementations for the above
apps/                    correctness_check, benchmark, pingpong
tests/                   unit tests (chunking) and MPI correctness/edge-case tests
analysis/                Python: fits the cost model, generates figures and tables
report/                  LaTeX source for the write-up (report/main.pdf is a build output)
scripts/                 sweep drivers, MPI algorithm introspection, env setup
results/sample_run/      real single-node Microsoft MPI dataset -- read its README.md
docs/                    architecture, algorithm derivation, benchmarking, design log
```

## Reproducing the correctness tests and a real sweep

Full instructions, including how to find your Open MPI's ring algorithm ID
(it has moved across releases, do not assume it) and known gotchas around
running as root or with fewer cores than ranks, are in
`docs/BENCHMARKING.md`. `docs/ARCHITECTURE.md` and `docs/ALGORITHM.md`
cover the implementation itself, and `docs/DESIGN_DECISIONS.md` is a
running log of every judgment call made while building this, including the
sandbox constraints that shaped several of them.

## License

[MIT](LICENSE).
