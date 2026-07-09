#!/usr/bin/env python3
"""Validates the ring-allreduce ALGORITHM (index arithmetic), independent of MPI.

Why this file exists: this repository's real correctness tests
(test_ring_allreduce_correctness.cpp, test_edge_cases.cpp) require an actual
MPI runtime to execute -- multiple communicating ranks are the entire point.
The sandbox this repository was originally built in has no MPI installed and
no network access to install one (see docs/DESIGN_DECISIONS.md), so those
tests could be written and reviewed but not run there.

This script is the substitute check that *could* be run in that environment:
it simulates N ranks inside a single Python process, using the exact same
send/recv index formulas as include/ring_allreduce/ring_allreduce.hpp
(reduce-scatter send_idx/recv_idx, all-gather send_idx/recv_idx), and checks
the result against an independently-computed ground truth. It validates the
ALGORITHM -- the thing a wrong index silently corrupts -- not the MPI
transport (real concurrent Isend/Irecv, actual non-blocking progress,
wire-format correctness), which genuinely does require running the real
test suite on a machine with MPI. Treat a clean run of this script as
necessary, not sufficient, evidence of correctness; run
`ctest --test-dir build` for the real thing once you have MPI.

Usage: python3 validate_ring_logic.py
Exit code 0 iff every case in the test matrix passes.
"""

import sys


def compute_chunks(count, n):
    """Mirrors src/chunking.cpp exactly (same remainder-distribution rule)."""
    base, remainder = divmod(count, n)
    sizes = [base + (1 if i < remainder else 0) for i in range(n)]
    offsets = []
    acc = 0
    for s in sizes:
        offsets.append(acc)
        acc += s
    return sizes, offsets


def simulate_ring_allreduce(local_buffers, op):
    """Mirrors include/ring_allreduce/ring_allreduce.hpp's two phases exactly.

    local_buffers: list of N sequences (one per simulated rank), each of the
    same length `count`. Returns a list of N lists: every rank's final,
    fully-reduced buffer.

    Send/receive for a given step are computed from a single start-of-step
    snapshot (the `outgoing` dict below), then applied -- mirroring the fact
    that in the real MPI code, every rank's Isend/Irecv for a step are issued
    concurrently against the state as of the end of the *previous* step, not
    against partial updates made by other ranks mid-step.
    """
    n = len(local_buffers)
    if n == 1:
        return [list(local_buffers[0])]

    count = len(local_buffers[0])
    sizes, offsets = compute_chunks(count, n)
    bufs = [list(b) for b in local_buffers]

    def next_rank(r):
        return (r + 1) % n

    def prev_rank(r):
        return (r - 1 + n) % n

    # Phase 1: reduce-scatter, N - 1 steps.
    for step in range(n - 1):
        outgoing = {}
        for r in range(n):
            send_idx = (r - step) % n
            off, sz = offsets[send_idx], sizes[send_idx]
            outgoing[r] = bufs[r][off:off + sz]
        for r in range(n):
            recv_idx = (r - step - 1) % n
            off, sz = offsets[recv_idx], sizes[recv_idx]
            incoming = outgoing[prev_rank(r)]
            assert len(incoming) == sz, (
                f"phase1 step={step} rank={r}: size mismatch, "
                f"sent {len(incoming)} expected {sz}"
            )
            for i in range(sz):
                bufs[r][off + i] = op(bufs[r][off + i], incoming[i])

    # Phase 2: all-gather, N - 1 steps (direct overwrite, no reduction).
    for step in range(n - 1):
        outgoing = {}
        for r in range(n):
            send_idx = (r - step + 1) % n
            off, sz = offsets[send_idx], sizes[send_idx]
            outgoing[r] = bufs[r][off:off + sz]
        for r in range(n):
            recv_idx = (r - step) % n
            off, sz = offsets[recv_idx], sizes[recv_idx]
            incoming = outgoing[prev_rank(r)]
            assert len(incoming) == sz
            for i in range(sz):
                bufs[r][off + i] = incoming[i]

    return bufs


def ground_truth(local_buffers, op):
    count = len(local_buffers[0])
    n = len(local_buffers)
    result = list(local_buffers[0])
    for r in range(1, n):
        for i in range(count):
            result[i] = op(result[i], local_buffers[r][i])
    return result


def run_case(n, count, op, op_name, seed_fn, label):
    local_buffers = [[seed_fn(r, i) for i in range(count)] for r in range(n)]
    expected = ground_truth(local_buffers, op)
    got = simulate_ring_allreduce(local_buffers, op)
    ok = True
    for r in range(n):
        if got[r] != expected:
            ok = False
            print(f"  MISMATCH rank={r} N={n} count={count} op={op_name} ({label})")
            print(f"    expected: {expected[:8]}{'...' if count > 8 else ''}")
            print(f"    got:      {got[r][:8]}{'...' if count > 8 else ''}")
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] N={n:>2} count={count:>10} op={op_name:<6} ({label})")
    return ok


def main():
    all_ok = True

    n_values = [1, 2, 3, 4, 5, 7, 8, 16]
    # count dimension per the spec's Sec. 3 test matrix: 0-or-1, N-1, N, N+1,
    # 1024, "a size spanning multiple ints" (represented here, at simulation
    # scale, by a few-thousand-element case -- a literal >INT_MAX buffer is
    # exercised only at the compute_chunks arithmetic level, in
    # test_chunking.cpp, where no real buffer allocation is required).
    for n in n_values:
        counts = sorted(set([0, 1, max(n - 1, 0), n, n + 1, 1024, 4001]))
        for count in counts:
            # SumOp with small integers: integer-valued floats sum exactly
            # regardless of reduction order (no floating-point epsilon
            # needed), so this is a bit-exact check, not an approximate one.
            all_ok &= run_case(
                n, count, lambda a, b: a + b, "Sum",
                lambda r, i: (r * 31 + i * 7) % 13, "small-int seed"
            )
            # MaxOp / MinOp are exactly associative+commutative regardless of
            # value magnitude or order.
            all_ok &= run_case(
                n, count, lambda a, b: max(a, b), "Max",
                lambda r, i: (r * 17 + i * 5) % 97 - 48, "signed-range seed"
            )
            all_ok &= run_case(
                n, count, lambda a, b: min(a, b), "Min",
                lambda r, i: (r * 17 + i * 5) % 97 - 48, "signed-range seed"
            )
            # ProdOp with powers of two: multiplication/division by a power
            # of two is exact in IEEE 754 regardless of order (pure exponent
            # shifts), so this stays bit-exact instead of needing a
            # tolerance-based comparison.
            all_ok &= run_case(
                n, count, lambda a, b: a * b, "Prod",
                lambda r, i: 2.0 ** (((r + i) % 5) - 2), "power-of-two seed"
            )

    print()
    if all_ok:
        print("ALL CASES PASSED:", len(n_values), "N values x full count/op matrix")
        return 0
    else:
        print("SOME CASES FAILED -- see MISMATCH lines above")
        return 1


if __name__ == "__main__":
    sys.exit(main())
