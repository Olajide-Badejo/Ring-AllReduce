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
**real measurement** against **Open MPI 5.0.10** on an i7-14700K: all three
algorithm variants, every process count N = 2..16 including the
non-power-of-two ones, across 8 B to 128 MiB (see
`results/sample_run/README.md` for full provenance). Every figure and table
in `report/main.pdf` derives from it.

The headline result comes from benchmarking `MPI_Allreduce` **two ways**:
as it selects algorithms by default, and forced via MCA parameters to run
Open MPI's *own ring*. At 8 B and N=16 the from-scratch ring takes 11.55 us
against the default's 2.26 us, roughly 5x slower, which in isolation looks
like an indictment of the implementation. But Open MPI's own ring, forced,
is just as slow (11.97 us vs this project's 11.08 us at 64 B, within ~8%),
which shows the gap is the price of the ring **algorithm**'s `2(N-1)` step
count, not a deficiency of this code. Above 64 KiB the custom ring is
actually the faster of the two rings, finishing 16 MiB at N=16 in 19.2 ms
against the vendor ring's 24.6 ms.

The one caveat: it is a single-node run (shared-memory transport), so bus
bandwidth peaks in the L3-cache-resident range and a single-slope Hockney
fit only loosely describes it (R^2 ~0.3). The report says so directly.

![Achieved bus bandwidth vs message size](docs/assets/busbw_preview.png)
*Real single-node Open MPI 5.0.10 measurements, rendered by
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
`make report` again. Run `scripts/check_mpi_algorithms.sh` first to confirm
your Open MPI's ring algorithm ID: it has moved across releases, so do not
assume 4.

## On Windows

There is no native Open MPI for Windows, and Microsoft MPI has no
`coll/tuned` MCA controls, so it cannot produce the forced `mpi-ring`
variant the report's central comparison depends on. Use WSL2, which is how
the committed dataset was measured on this workstation:

```bash
wsl --install                      # elevated, one time; then reboot
sudo apt-get update
sudo apt-get install -y openmpi-bin libopenmpi-dev cmake build-essential
```

Then follow the quickstart above inside WSL. Build on the WSL native
filesystem rather than under `/mnt/c` (CMake fails with "Operation not
permitted" on the Windows drive mount). To build the PDF or run the analysis
from Windows itself, use `make PYTHON=py report`, since the bare `python3`
on a Windows PATH is often the MSYS build with no numpy.

## Directory overview

```
include/ring_allreduce/  the algorithm: chunking, reduce ops, timer, ring_allreduce
src/                     implementations for the above
apps/                    correctness_check, benchmark, pingpong
tests/                   unit tests (chunking) and MPI correctness/edge-case tests
analysis/                Python: fits the cost model, generates figures and tables
report/                  LaTeX source for the write-up (report/main.pdf is a build output)
scripts/                 sweep drivers, MPI algorithm introspection, env setup
results/sample_run/      real single-node Open MPI 5.0.10 dataset -- read its README.md
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
