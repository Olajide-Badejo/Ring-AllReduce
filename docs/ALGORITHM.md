# Algorithm

This is the derivation behind `include/ring_allreduce/ring_allreduce.hpp`,
written out in full because the index arithmetic is exactly the kind of
thing that is easy to get subtly wrong and have it still look like it works
on the first test case you try. See `report/sections/02_background.tex` for
the same material presented for a reader rather than an implementer, and
`tests/validate_ring_logic.py` for a from-scratch simulation that checks
these formulas against a numpy ground truth for every N and count this
project's test matrix covers.

## Setup

N ranks, numbered 0 to N-1. Every rank holds a local buffer of `count`
elements. `compute_chunks(count, N)` splits that buffer into N contiguous
chunks, indexed 0 to N-1, as evenly as possible (chunk sizes differ by at
most one element; see `docs/DESIGN_DECISIONS.md` for the exact
remainder rule and how it degrades gracefully when `count < N`).

```
next = (rank + 1) mod N
prev = (rank - 1 + N) mod N
```

## Phase 1: reduce-scatter, N-1 steps

At step `s` (0-indexed, `s` from 0 to N-2), rank `r`:

```
send_idx = (r - s + N) mod N
recv_idx = (r - s - 1 + N) mod N
```

sends its current copy of chunk `send_idx` to `next`, and concurrently
receives a chunk from `prev` into a scratch buffer, which it then combines
elementwise (via the reduction functor) into its own copy of chunk
`recv_idx`.

Why these particular indices: trace N=4 by hand. At step 0, rank r sends
its own chunk `r` and receives into chunk `(r-1) mod N`. Rank 0 sends
chunk 0 to rank 1 and receives (from rank 3) into chunk 3; rank 1 sends
chunk 1 to rank 2 and receives (from rank 0) into chunk 0; and so on
around the ring. Each subsequent step shifts both indices back by one. After
N-1 steps, rank r has accumulated a contribution from every other rank into
exactly one chunk: index `(r+1) mod N`. This is not an incidental property
of the specific example; it holds for every N, and is exactly what
`tests/validate_ring_logic.py` checks exhaustively rather than for one
hand-traced case.

## Phase 2: all-gather, N-1 steps

Now every rank holds one fully-reduced chunk, and the N fully-reduced
chunks need to end up on every rank. At step `s`, rank `r`:

```
send_idx = (r - s + 1 + N) mod N
recv_idx = (r - s + N) mod N
```

sends chunk `send_idx` to `next` and receives chunk `recv_idx` from `prev`,
this time overwriting its local copy directly rather than reducing into it,
since the incoming chunk is already fully reduced. The same rotate-by-one
structure as reduce-scatter circulates the N complete chunks around the
ring until every rank has all of them.

## Why 2(N-1) steps, and why that is bandwidth-optimal

Total steps: `2(N-1)`, exactly. Each step, every rank sends and receives
one chunk, roughly `count/N` elements. Total data sent (and received) by
any single rank across the entire operation is:

```
2 * count * (N-1)/N * sizeof(T) bytes
```

which approaches, and never exceeds, `2 * count * sizeof(T)` bytes as N
grows. That bound is independent of N: adding more ranks does not increase
how much data any individual rank has to move. That is the bandwidth-optimal
property (Patarasuk & Yuan, cited in the report) that makes this algorithm
the standard choice for large-scale collective communication, and it is
also exactly the `2(N-1)/N` factor `apps/benchmark.cpp` uses to convert
algorithm bandwidth into bus bandwidth (see `report/sections/05_results.tex`,
"Bandwidth metrics and cost model").

## What is NOT optimal about it

`2(N-1)` steps grows linearly in N. Algorithms based on recursive halving
and doubling (see `docs/DESIGN_DECISIONS.md`'s reference to Rabenseifner's
algorithm) reach the same asymptotic bandwidth optimality in only
`O(log N)` steps. At small message sizes, where per-step latency rather
than bandwidth dominates total time, that step-count difference is the
entire story behind why a correct, bandwidth-optimal ring can still lose to
a vendor MPI implementation's own algorithm selection. `report/sections/06_discussion.tex`
walks through exactly when and why.

## Edge cases, and why each is handled where it is

- **N == 1**: handled by an early return in `allreduce`, before any chunk
  computation, communication, or scratch-buffer allocation happens. There
  are no ring neighbors when there is only one rank, and nothing to combine
  the local buffer with, so the buffer is already correct.
- **count < N**: `compute_chunks` naturally produces size-0 trailing
  chunks in this case (see its own docstring), and `allreduce` always
  issues both sides of a step's `Isend`/`Irecv`/`Waitall` handshake
  regardless of whether the payload for that step happens to be empty. A
  zero-byte MPI message is completely legal; skipping a step's
  synchronization when a chunk is empty would desync the ranks whose
  neighbor's chunk that step is not empty, corrupting the ring's lockstep
  progression for everyone downstream of that rank.
- **Non-power-of-two N**: every formula above is plain modular arithmetic
  with no power-of-two assumption anywhere; `tests/test_ring_allreduce_correctness.cpp`
  and `tests/test_edge_cases.cpp` both include N in {3, 5, 7} specifically
  so that stays true rather than merely assumed.
