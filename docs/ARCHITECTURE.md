# Architecture

## Module responsibilities

```
include/ring_allreduce/
  chunking.hpp       compute_chunks(count, N): pure, MPI-free arithmetic
  reduce_ops.hpp      SumOp/MaxOp/MinOp/ProdOp functors, MPI-free
  timer.hpp           thin MPI_Wtime wrapper
  ring_allreduce.hpp  allreduce<T, Op>: the algorithm itself, header-defined
src/
  chunking.cpp        compute_chunks implementation
  ring_allreduce.cpp  ring_neighbors() + explicit template instantiations
apps/
  correctness_check   hand-runnable sanity driver
  benchmark           the main benchmark harness
  pingpong            raw MPI_Send/Recv latency reference
tests/
  test_chunking.cpp                    MPI-free, real unit tests
  test_ring_allreduce_correctness.cpp  full N x count x dtype x op matrix
  test_edge_cases.cpp                  N==1, zero-size chunks, non-power-of-two N
analysis/             Python: fit the cost model, generate figures/tables
report/               LaTeX source; figures/tables are generated, not tracked
```

`chunking.hpp`/`.cpp` and `reduce_ops.hpp` have zero MPI dependency by
design: everything else in this library needs MPI, but these two pieces are
pure host-side logic, which is exactly what makes them independently
testable with an ordinary compiler and no `mpirun` in the loop (see
`tests/test_chunking.cpp`, and `docs/DESIGN_DECISIONS.md` for why that
mattered a great deal while this repository was being built).

## Why non-blocking point-to-point, not blocking

Every step of both the reduce-scatter and all-gather phases issues one
`MPI_Isend` and one `MPI_Irecv` before a single `MPI_Waitall(2, ...)`. A
blocking `MPI_Send` followed by `MPI_Recv` would serialize the two halves of
every step: every rank would need to wait for its send to complete before
even starting to look for an incoming message, when in fact both directions
of a step's handshake are supposed to happen concurrently. That would
roughly double the measured wall-clock time per step without moving a
single additional byte, which would make every downstream bandwidth number
wrong in a way that would not be obvious just from looking at the numbers
(they would still look like a plausible ring-allreduce, just a badly slow
one). Non-blocking point-to-point is not a performance nicety here; it is
required for the implementation to mean what it claims to measure.

## Buffer-reuse strategy across benchmark iterations

`allreduce<T, Op>`'s public signature is `(T* buf, size_t count, MPI_Comm
comm)`, in place, with no scratch-buffer parameter. The reduce-scatter
phase still needs somewhere to receive an incoming chunk before combining it
into the local buffer. Rather than allocate and free that scratch buffer on
every single call, `detail::scratch_buffer<T>()` returns a `thread_local`
`std::vector<T>` that only ever grows (resized up if too small, never
shrunk back down), reused across calls.

This matters specifically because of how this buffer gets exercised in
practice: `apps/benchmark` calls `allreduce` many times per message size in
a tight timed loop, specifically to build up enough repetitions for a
meaningful median and standard deviation. Paying a heap allocation inside
that loop would add allocator noise directly into the smallest-message
timing numbers, which is exactly the regime this whole project cares about
getting right (Section on pipeline fill latency in `docs/ALGORITHM.md` and
the report's discussion section). `thread_local` rather than a plain
function-local `static` is defensive design for a hypothetical
multiple-threads-per-rank caller; this project's own code, and every MPI
program in this repository, is one OS thread per rank, so that defensiveness
costs nothing in practice.

## Error handling

MPI's default error handler (`MPI_ERRORS_ARE_FATAL`) is left in place;
nothing in this codebase catches or translates an MPI error code. This is
standard practice for a benchmark-oriented MPI program rather than a
fault-tolerant service, and keeps the code that matters (the algorithm
itself) free of error-handling boilerplate that would not change behavior
in the cases this project cares about. `detail::chunk_count_to_mpi_int`
(see `docs/DESIGN_DECISIONS.md`) is the one place this codebase does throw
deliberately, guarding against silent `int` truncation of a chunk size
rather than an MPI call itself failing.
