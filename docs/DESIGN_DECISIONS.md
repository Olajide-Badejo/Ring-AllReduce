# Design decisions

A running log of judgment calls made while building this repository, in
roughly the order they came up, with the reasoning behind each. The spec
this project was built from asked for exactly this: where something was not
fully pinned down, make the industry-standard choice, implement it, and
record why here rather than silently picking one option.

## Real Open MPI measurement: WSL2, not Microsoft MPI

On 2026-07-12 this project was built, tested, and benchmarked against a real
Open MPI 5.0.10 installation on the target i7-14700K workstation. The
committed dataset at `results/sample_run/` is that measurement.

**Why WSL2 rather than the Windows-native MPI.** The workstation runs
Windows, and the obvious route was Microsoft MPI (which does work: an earlier
iteration built cleanly against it, passed all 17 ctest cases, and produced a
real two-variant sweep). It was abandoned because Microsoft MPI has **no
equivalent of Open MPI's `coll/tuned` MCA controls**, so it cannot force
`MPI_Allreduce` to run the vendor's own ring. That forced ring-versus-ring
run is not a nice-to-have: it is the single experiment that separates "my
implementation is slow" from "the ring algorithm is slow," which is the
report's central claim. Without it the benchmark can only compare the custom
ring against a vendor default that is silently running a different, lower-step
algorithm, which measures algorithm choice and implementation quality mixed
together and cannot tell them apart. There is no native Open MPI for Windows,
so WSL2 (Ubuntu, same physical CPU) was the way to get a spec-compliant
three-variant run. It was worth the setup.

What the real run covers: Open MPI 5.0.10, ring algorithm id 4 (read from
`ompi_info`, not assumed), all three variants (`ring`, `mpi-default`,
forced `mpi-ring`), every N from 2 to 16 including non-power-of-two, 8 B
through 128 MiB, on 28 hardware threads so nothing is oversubscribed. All 17
ctest cases pass against this Open MPI.

**The payoff.** The custom ring is ~5x slower than Open MPI's default at 8 B
(11.55 us vs 2.26 us at N=16), which alone reads as an implementation failure.
Forcing Open MPI to run its own ring shows it is just as slow (11.97 us vs
this project's 11.08 us at 64 B, within ~8%), proving the gap is the ring
algorithm's `2(N-1)` step count, not the code. Above 64 KiB the custom ring
beats the vendor's own ring. That is a far more defensible result than either
the synthetic data or the Microsoft MPI two-variant run could support.

**Remaining limitation:** single node, so the transport is shared memory, not
a fabric. Bus bandwidth therefore peaks in the L3-cache-resident range and the
single-slope Hockney fit is loose (R^2 ~0.28 to 0.33). Documented wherever
the data is cited; a multi-node sweep is the top follow-up.

## Line endings: `.gitattributes` forces LF on shell scripts

Discovered the hard way. Git's `autocrlf` gives a Windows checkout CRLF line
endings, and the repo blob stores LF, so CI (Linux) was always fine. But
running the sweep scripts from a Windows working tree inside WSL fails with
`set: pipefail: invalid option name`, because bash sees the trailing `\r` as
part of the option name. `.gitattributes` now pins `*.sh` and `*.sbatch` to
`eol=lf` so they stay runnable on Linux/WSL regardless of where they were
checked out.

## Initial sandbox constraints (historical)

The initial repository implementation was developed in a sandbox with **no MPI installation and no
network access to install one** (`apt-get install` and `pip install` both
fail with no route to a package index; there is no local package cache with
OpenMPI, MPICH, or CMake in it either). This is the root cause behind most of
the decisions below, so it is worth stating plainly once instead of
repeating it in every entry:

- The C++/MPI code (`include/`, `src/`, `apps/`, the MPI-dependent test
  files) was written carefully, compiled and executed for real wherever it
  had no MPI dependency (`chunking.cpp`, the `reduce_ops` functors), and
  syntax/type-checked for everything else against a minimal stand-in
  `mpi.h` built only for this purpose and never copied into the shipped
  repository. Explicit template instantiation in `src/ring_allreduce.cpp`
  was used specifically so that stand-in compile forces the compiler to
  fully instantiate and type-check `allreduce<T, Op>`'s body, not just
  parse its declaration.
- The ring algorithm's index arithmetic (the part most likely to hide an
  off-by-one) was additionally validated by
  `tests/validate_ring_logic.py`, a transport-independent simulation that
  reproduces the exact same send/receive formulas across the full N and
  count test matrix and was actually run, with all cases passing.
- `results/sample_run/`'s data was, during this sandbox phase, synthetic:
  generated from the Hockney cost model plus noise, specifically so the
  Python analysis pipeline and the LaTeX report could be exercised and
  reviewed end to end before any real MPI existed. That placeholder has since
  been replaced by the real Open MPI 5.0.10 sweep (see the first entry above),
  and the generator script has been deleted. `results/sample_run/` now
  contains real measurements and nothing else.
- CMake itself is also not installed in this sandbox, so the CMake build
  was authored to standard practice but never configured or built here;
  `g++` was used directly (bypassing CMake) to compile and, where possible,
  run individual translation units instead.

This history remains useful because it explains why the committed sample
dataset was synthetic for much of the project's life and why the analysis
pipeline was designed to be validated without real hardware. The real
Windows MPI verification above supersedes that limitation: the committed
dataset is now a real measurement.

## C++ standard: C++20, not C++17

The build spec states "C++20" as a non-negotiable up front, but its build
system section separately says "C++17". These conflict. C++20 was chosen
because it was the one explicitly flagged non-negotiable; the C++17
mention is treated as the stale one. `CMakeLists.txt` sets
`CMAKE_CXX_STANDARD 20` and says so in a comment pointing back here.

## clang-format: Google style, not LLVM

Both are reasonable defaults for a C++ project with no house style of its
own; Google's is used unmodified (`BasedOnStyle: Google` plus a 100-column
limit) rather than customized, since inventing a bespoke style was
explicitly what the spec asked not to do.

## Bespoke test harness instead of Catch2 or GoogleTest

The spec's own preference is Catch2 or GoogleTest. Neither is available
here: Catch2's single-header amalgamation isn't vendored anywhere on disk
and there is no network access to fetch it, and `libgtest-dev` is not
installed and cannot be `apt-get install`ed offline. `tests/micro_test.hpp`
is a small, honest, from-scratch harness (`TEST_CASE`/`REQUIRE`/`CHECK`,
mirroring Catch2's own names) rather than a silent claim to be using either
real framework. Migrating to real Catch2 later is close to a
find-and-replace, by design: the macro names match, so existing test files
would need only the include swapped and `RUN_ALL_TESTS()` replaced with
Catch2's own generated `main`.

## MIT license copyright holder

Set to "the ring-allreduce contributors" rather than a person's name, since
this project has no way to know who is filling in a name here; this is a
common, legitimate pattern for open-source projects and is easy to replace
with a real name or organization.

## Explicit template instantiation alongside a header-defined template

`allreduce<T, Op>` is fully defined inline in `ring_allreduce.hpp` (so any
caller can instantiate it for any `T`/`Op` it wants), but
`src/ring_allreduce.cpp` also explicitly instantiates the eight
combinations this project's own tests, `correctness_check`, and
`benchmark` actually use. This is not required for correctness. It exists
so this library's own build typechecks the template body immediately
(valuable precisely because this sandbox cannot otherwise compile it
against real MPI to find out), and so the common float/double x SumOp path
every benchmark run takes is compiled once here rather than redundantly in
every translation unit that calls it.

## Buffer-reuse strategy

`allreduce`'s reduce-scatter phase needs a scratch receive buffer, and the
public API is a fixed `(T*, count, comm)` signature with nowhere to pass one
in. A `thread_local` buffer that only ever grows (never shrinks, never
reallocated smaller) is used instead of a fresh allocation per call,
specifically because `apps/benchmark` calls this function thousands of
times per message size in a tight loop, and allocator overhead landing
inside that loop would show up as noise in exactly the small-message
latency numbers the whole benchmark exists to measure cleanly. `thread_local`
rather than a plain function-local static is pure defensiveness for a
hypothetical multi-threaded-per-rank caller; this project's own code is one
OS thread per rank.

## Zero-count MPI buffers use a non-null scratch address

The public API deliberately permits `allreduce(nullptr, 0, comm)`, and an
empty `MPI_Isend` or `MPI_Irecv` is a required part of every ring step even
when a chunk has no elements. MPI ignores the buffer for a zero-count
operation, but forming `nullptr + 0` is undefined behavior in C++. The
thread-local scratch buffer therefore retains at least one element and is
used as the message address for the zero-count case. This preserves both
the documented API and the required step synchronization without relying on
implementation-specific behavior of an empty `std::vector`.

## Benchmark repetitions restore their rank-local input

Allreduce mutates its input buffer in place. The benchmark restores each
rank's deterministic input immediately before the barrier that precedes
every warmup and timed iteration, so initialization is excluded from the
timed region while every measurement is an independent SUM operation.
Without this reset, repeated iterations would reduce the previous result
again and could overflow or saturate floating-point values during a long
small-message run.

## No MPI big-count ("_c") API support

MPI point-to-point calls take an `int` count. A single chunk's size is
guarded (`detail::chunk_count_to_mpi_int`) and throws rather than silently
truncating if it would ever exceed `INT_MAX` elements. This project targets
Open MPI 4.x/5.x's ordinary point-to-point API, matching how virtually
every production ring-allreduce (including Horovod's) is written, rather
than adopting MPI 4.0's big-count `_c` variants. At this project's largest
benchmarked message (128 MiB) and smallest N (2), the largest single chunk
is nowhere near that limit, so this is a documented scope boundary, not a
live constraint on anything this codebase measures.

## Where "a count spanning multiple ints" is actually tested

Testing a real element count above `INT_MAX` through the full point-to-point
pipeline would need tens of gigabytes of real buffers per rank, multiplied
by up to 16 ranks: impractical for CI or typical dev hardware, and
definitely impractical in this sandbox's 3.9 GB. `compute_chunks` itself,
though, only ever returns two length-N vectors regardless of `count`, so its
arithmetic can be, and is, checked at that scale for free
(`tests/test_chunking.cpp` uses a 6,000,000,000-element count). The
MPI-level correctness tests use a "large but memory-safe" count
(`kLargeCount`, a few million elements) instead, documented in both test
files as deliberately not the same thing.

## CSV schema: long format, one row per statistic

`apps/benchmark`'s output has one row per `(algorithm, num_procs, dtype,
message_size_bytes, time_stat)` combination rather than one row per
configuration with five separate time columns. This tidy/long layout is
what `analysis/load_results.py`'s `rows_for_stat()` filters against, and it
is what lets `analysis/run_full_analysis.py` avoid any reshaping step.

## `mpi-default` and `mpi-ring` execute identical code

Forcing Open MPI's tuned collective to a specific algorithm is MCA
parameter state read once at `MPI_Init`, which means it is a property of
how `mpirun` itself was invoked, not something `apps/benchmark` can flip
mid-run. `--algorithm mpi-default` and `--algorithm mpi-ring` therefore call
the exact same `MPI_Allreduce`; what differs is which `mpirun` invocation
(with or without the `--mca coll_tuned_*` forcing flags) the operator used,
which `scripts/run_local_sweep.sh` handles by launching each `N` twice. A
best-effort runtime check (`warn_if_mca_label_mismatch` in
`apps/benchmark.cpp`) compares the requested label against the
`OMPI_MCA_coll_tuned_*` environment variables Open MPI sets when `--mca` is
used, and warns (not fails) on an apparent mismatch, since this check is
Open-MPI-specific and best-effort rather than guaranteed portable.

## Adaptive iteration count

Each benchmark configuration times a variable number of repetitions,
floored at a minimum (3 for `benchmark`, 5 for `pingpong`, so standard
deviation means something) and capped by both a wall-clock budget per
configuration and a hard maximum rep count, rather than a single fixed
repetition count for every message size. A fixed count would either waste
enormous time on the smallest, fastest messages or badly undersample the
largest, slowest ones.

## `median`, not `min`, as "achieved" performance

`analysis/generate_plots.py` and `generate_tables.py` both fix
`ACHIEVED_STAT = "median"`. Minimum is the conventional alternative in some
benchmarking traditions, but it reports the single best-case sample rather
than typical behavior, which is a worse match for what this report is
actually trying to characterize; median is far less sensitive than the mean
to an occasional unfavorable scheduling accident while remaining far less
optimistic than the minimum.

## Weighted, not unweighted, least squares for the alpha/beta fit

This is the one decision in this list that was actually validated
empirically rather than argued for on paper alone. The message-size sweep
spans roughly seven orders of magnitude (8 B to 128 MiB), so an unweighted
least-squares fit of Equation (the Hockney model) is dominated by the
handful of largest, slowest configurations: beta (the bandwidth term, which
sets those large values) comes out precisely, but alpha (whose entire
contribution to T is only visible at the smallest sizes) is comparatively
unconstrained and can be badly biased even when R^2 looks excellent.
This needs evidence, and evidence requires knowing the right answer in
advance, which real measurements do not come with. So the fitter is checked
against data synthesized from known constants, the same way a numerical solver
is verified against a manufactured solution. That check is
`analysis/validate_fit.py`, it writes no files, and it is reproducible: run
`python3 analysis/validate_fit.py`. Unweighted OLS recovers beta to within a
fraction of a percent but misses alpha by up to **17%**; weighting each row by
1/T (turning the fit into a relative-error, not absolute-error, minimization)
recovers both to within **0.4%**. It exits nonzero if that ever stops holding.
`analysis/theoretical_model.py`'s `fit_alpha_beta` implements the weighted
version.

Two notes on doing this honestly. The synthetic data exists *only* to validate
the fitter; it is never analyzed, plotted, or reported, and
`results/sample_run/` contains real Open MPI measurements and nothing else.
And each synthetic point is the median of many noisy repetitions, matching what
`apps/benchmark.cpp` actually reports and what the pipeline actually fits.
Fitting a single raw draw per point instead would feed the fitter noisier input
than it ever sees in production and would understate how well it does (it did,
when first written: the weighted fit's worst error looked like 1.3% rather than
0.35%).

## Summary table reported at N = 16, not averaged across N

`generate_tables.py`'s busbw summary table (Table in
`report/sections/05_results.tex`) reports a single process count, the
largest in the sweep, rather than an average across N. Averaging across N
would blend together configurations with genuinely different busbw at the
same message size and would not describe any one real configuration; a
specific, named N is more honest about what the number in the table
actually means.

## The report's own no-em-dash constraint

The build spec asked for no em dashes or en dashes anywhere in this
project. Every file was checked directly for the actual Unicode characters
(`grep -P '[\x{2014}\x{2013}]'`), with zero matches. A subtler risk was
checked too: LaTeX's body-text font ligature converts a literal `--` into a
rendered en dash glyph even though the `.tex` source never contains the
Unicode character itself, confirmed with a small standalone test
(`1--10` renders as `1` + en dash + `10` in the compiled PDF). Every `--` in
the report's prose lives inside `\texttt{...}` (command-line flags like
`--mca`), which does not trigger that ligature, confirmed with the same
test; the only other `--` in the report is the standard BibTeX page-range
convention (`pages = {117--124}`), matching the exact convention the spec's
own given bibliography entries use, and treated as a distinct typographic
convention rather than a stylistic dash.

## The compiled report PDF is a build artifact, not tracked source

`report/main.pdf`, like `report/figures/*.pdf` and `report/tables/*.tex`,
is gitignored. `.github/workflows/report.yml` builds it and uploads it as a
CI artifact, which is the same model: generated on demand, not committed.
`make report` regenerates it locally in well under a minute from the
committed sample dataset.
