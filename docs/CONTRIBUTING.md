# Contributing

This started as a single-author systems project, so this file is short:
enough structure to keep it consistent if it grows, not a process built for
a team that does not exist yet.

## Style

- C++: `.clang-format` (Google style, unmodified, 100-column limit) is the
  source of truth. Run `clang-format -i` on anything you touch before
  committing; CI does not currently enforce this automatically, but it will
  reject anything that also fails `-Wall -Wextra -Wpedantic` with
  `-Werror` (see `RING_ALLREDUCE_WARNINGS_AS_ERRORS` in the top-level
  `CMakeLists.txt`).
- Python: no formatter is currently wired in; match the existing style in
  `analysis/` (standard library + numpy/pandas conventions, docstrings on
  every public function).
- Commit messages: a short summary line, then a body explaining *why*, not
  just *what*, especially for anything that was a judgment call. If a
  change involves a decision that is not obvious from the code itself, add
  an entry to `docs/DESIGN_DECISIONS.md` in the same commit.
- No em dashes or en dashes anywhere: code, comments, commit messages, or
  docs. Use a comma, a colon, a semicolon, or a parenthetical instead.

## Before opening a PR

1. `cmake --build build && ctest --test-dir build --output-on-failure` must
   be green. This is non-negotiable for anything touching `include/`,
   `src/`, or `tests/`; a change to the core algorithm without a passing
   test suite is not reviewable.
2. If you changed anything under `analysis/`, run
   `python3 analysis/run_full_analysis.py` against
   `results/sample_run/` and confirm it still completes without error.
3. If you changed the report's prose or the analysis pipeline's figures or
   tables, rebuild the PDF (`make report` from the repository root) and
   skim it; do not assume a `.tex` edit compiles cleanly without checking.
4. If you touched anything MPI-related, ideally test at more than one
   process count, including a non-power-of-two one (N=3, 5, or 7), not
   only N=2 or N=4.

## Adding a new reduction operator

`include/ring_allreduce/reduce_ops.hpp`'s functors are the pattern:
a struct with a templated `operator()(T a, T b) const`. Add the struct
there, add it to the explicit instantiation list at the bottom of
`src/ring_allreduce.cpp` for the `float`/`double` combinations you expect
to use, and add a corresponding case to `tests/test_chunking.cpp`'s
functor tests (choose seed values deliberately, the way the existing ones
do, so equality checks stay exact rather than needing a floating-point
tolerance).

## Adding a new element type

Add a `detail::MpiDatatype<T>` specialization in
`include/ring_allreduce/ring_allreduce.hpp` mapping it to the right
`MPI_Datatype`, then extend the explicit instantiations in
`src/ring_allreduce.cpp` and the correctness test matrix
(`tests/test_ring_allreduce_correctness.cpp`) to cover it.
