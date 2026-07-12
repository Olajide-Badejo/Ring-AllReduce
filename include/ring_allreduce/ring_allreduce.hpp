#pragma once

#include <mpi.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ring_allreduce/chunking.hpp"
#include "ring_allreduce/reduce_ops.hpp"

namespace ring_allreduce {

namespace detail {

/// Returns {next, prev} ring-neighbor ranks for a bidirectional ring of size
/// `n`: `next = (rank + 1) mod n`, `prev = (rank - 1 + n) mod n`. Defined out
/// of line in src/ring_allreduce.cpp (rather than inline here) so it is
/// compiled once instead of duplicated into every allreduce<T, Op>
/// instantiation.
///
/// Precondition: `n >= 2` (callers handle n == 1 as a no-op before reaching
/// here, since a single rank has no ring neighbors).
std::pair<int, int> ring_neighbors(int rank, int n);

/// Maps a C++ element type to its MPI_Datatype. Specialized for float and
/// double per the "template over at least float and double" requirement;
/// the unspecialized primary template is a compile error so an unsupported
/// T fails to build instead of silently doing the wrong thing.
template <typename T>
struct MpiDatatype {
  static_assert(sizeof(T) == 0,
                "ring_allreduce::allreduce<T, Op>: no MPI_Datatype mapping for this T "
                "(only float and double are supported -- add a specialization here to add one)");
};

template <>
struct MpiDatatype<float> {
  static MPI_Datatype get() { return MPI_FLOAT; }
};

template <>
struct MpiDatatype<double> {
  static MPI_Datatype get() { return MPI_DOUBLE; }
};

/// Per-T reusable scratch receive buffer for the reduce-scatter phase.
///
/// Why this exists: benchmark.cpp calls allreduce<T, Op> thousands of times
/// in a tight loop across a message-size sweep. Without reuse, every single
/// call would malloc/free its temporary receive buffer, and that allocator
/// overhead is exactly the kind of noise that would contaminate the
/// small-message latency numbers this project's whole analysis depends on
/// (see docs/ARCHITECTURE.md, "buffer-reuse strategy"). The buffer grows
/// monotonically (resize only if too small) and is never shrunk.
///
/// thread_local rather than a plain function-local static purely for
/// defensiveness if this is ever called from more than one host thread per
/// rank; this project's own code is single-threaded per rank.
template <typename T>
std::vector<T>& scratch_buffer() {
  thread_local std::vector<T> buf;
  return buf;
}

/// Converts a chunk size to the `int` MPI's point-to-point API expects,
/// throwing instead of silently truncating if a single chunk ever exceeds
/// INT_MAX elements.
///
/// This project deliberately targets MPI-3's ordinary (non "_c" / big-count)
/// point-to-point API, matching the pinned Open MPI 4.x/5.x target and how
/// virtually every production ring-allreduce (including Horovod's) is
/// written in practice. At this project's largest benchmarked message
/// (128 MiB) and smallest N (2), the largest single chunk is 64 MiB / 4B =
/// 16Mi float elements -- far below INT_MAX (~2.147G) -- so this is a
/// documented scope boundary, not a live limitation for anything this
/// codebase measures. See docs/DESIGN_DECISIONS.md.
inline int chunk_count_to_mpi_int(std::size_t chunk_size) {
  if (chunk_size > static_cast<std::size_t>(INT_MAX)) {
    throw std::overflow_error(
        "ring_allreduce: a single chunk exceeds INT_MAX elements, which the point-to-point "
        "(non _c) MPI API this project uses cannot express in one Isend/Irecv");
  }
  return static_cast<int>(chunk_size);
}

}  // namespace detail

/// Bandwidth-optimal ring-allreduce built from raw MPI point-to-point calls.
///
/// Implements the classic two-phase ring-allreduce (Patarasuk & Yuan, 2009):
/// a reduce-scatter phase followed by an all-gather phase, each exactly
/// `N - 1` steps for `N` ranks, using only MPI_Isend / MPI_Irecv /
/// MPI_Waitall -- no MPI collective call appears anywhere in this function.
/// Send and receive are always issued concurrently within a step; see
/// docs/ALGORITHM.md for the full index derivation and docs/ARCHITECTURE.md
/// for why non-blocking point-to-point is required here (a blocking
/// send-then-recv would serialize every step and silently double the
/// measured per-step time).
///
/// The buffer is reduced in place: every rank passes its own `count`-element
/// contribution in `buf`, and every rank ends with the fully-reduced result
/// in `buf` (mirroring `MPI_IN_PLACE` semantics for `MPI_Allreduce`).
///
/// Special cases handled as first-class behavior, not bolted-on edge cases:
///   - `size(comm) == 1`: no-op passthrough (nothing to combine with).
///   - `count < size(comm)`: some chunks are legitimately size 0; this
///     function still issues (empty) MPI_Isend/MPI_Irecv for them rather
///     than skipping a step's synchronization, because a zero-byte message
///     is a completely legal MPI operation and skipping it would desync the
///     ring's step-by-step handshake for the ranks that *do* have data.
///   - non-power-of-two `size(comm)`: the modular index arithmetic below
///     makes no power-of-two assumption anywhere.
///
/// @tparam T   element type. Must have a `detail::MpiDatatype<T>`
///             specialization; `float` and `double` are provided.
/// @tparam Op  binary reduction functor invoked as `Op{}(T a, T b) -> T`,
///             e.g. SumOp (default), MaxOp, MinOp, ProdOp.
/// @param buf   pointer to (at least) `count` valid, initialized `T`s on
///              every rank; overwritten in place with the reduced result.
///              May be `nullptr` iff `count == 0`.
/// @param count number of elements in `buf` (the same value on every rank).
/// @param comm  communicator to reduce over. Every rank in `comm` must call
///              this collectively with identical `count`, `T`, and `Op`.
template <typename T, typename Op = SumOp>
void allreduce(T* buf, std::size_t count, MPI_Comm comm) {
  int rank = 0;
  int n = 0;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &n);

  if (n == 1) {
    return;  // Nothing to combine with; buf is already the (only) answer.
  }

  const ChunkLayout layout = compute_chunks(count, static_cast<std::size_t>(n));
  const auto [next, prev] = detail::ring_neighbors(rank, n);
  const MPI_Datatype dtype = detail::MpiDatatype<T>::get();
  Op op{};

  std::size_t max_chunk = 0;
  for (const std::size_t s : layout.sizes) max_chunk = std::max(max_chunk, s);
  std::vector<T>& tmp = detail::scratch_buffer<T>();
  // MPI ignores the buffer argument for a zero-count operation, but C++
  // still must not form `nullptr + 0` while preparing that argument. Keep a
  // one-element scratch allocation available as a safe, non-null dummy for
  // the legal `buf == nullptr, count == 0` API case documented above.
  if (tmp.size() < std::max<std::size_t>(max_chunk, 1)) {
    tmp.resize(std::max<std::size_t>(max_chunk, 1));
  }
  T* const mpi_buffer = count == 0 ? tmp.data() : buf;

  // Distinct tags per phase: not required for correctness in *this*
  // strictly-synchronous (Waitall every step) implementation, since at most
  // one message per direction is ever in flight between a given pair of
  // ranks -- but it costs nothing and keeps the door open for the segmented
  // /pipelined stretch goal (Sec. 10) where steps could overlap.
  constexpr int kReduceScatterTag = 100;
  constexpr int kAllGatherTag = 200;

  // --- Phase 1: reduce-scatter, N - 1 steps -------------------------------
  // Step s: rank sends its current copy of chunk (rank - s) mod N to `next`
  // and concurrently receives chunk (rank - s - 1) mod N from `prev` into a
  // scratch buffer, then reduces it into its own copy of that chunk. After
  // N - 1 steps, this rank holds the fully-reduced chunk (rank + 1) mod N.
  for (int step = 0; step < n - 1; ++step) {
    const int send_idx = ((rank - step) % n + n) % n;
    const int recv_idx = ((rank - step - 1) % n + n) % n;

    const std::size_t send_off = layout.offsets[static_cast<std::size_t>(send_idx)];
    const std::size_t send_size = layout.sizes[static_cast<std::size_t>(send_idx)];
    const std::size_t recv_off = layout.offsets[static_cast<std::size_t>(recv_idx)];
    const std::size_t recv_size = layout.sizes[static_cast<std::size_t>(recv_idx)];

    MPI_Request requests[2];
    MPI_Isend(mpi_buffer + send_off, detail::chunk_count_to_mpi_int(send_size), dtype, next,
               kReduceScatterTag, comm, &requests[0]);
    MPI_Irecv(tmp.data(), detail::chunk_count_to_mpi_int(recv_size), dtype, prev,
               kReduceScatterTag, comm, &requests[1]);
    MPI_Waitall(2, requests, MPI_STATUSES_IGNORE);

    for (std::size_t i = 0; i < recv_size; ++i) {
      mpi_buffer[recv_off + i] = op(mpi_buffer[recv_off + i], tmp[i]);
    }
  }

  // --- Phase 2: all-gather, N - 1 steps -----------------------------------
  // Step s: rank sends its (now fully-reduced) copy of chunk
  // (rank - s + 1) mod N to `next` and concurrently receives chunk
  // (rank - s) mod N from `prev`, overwriting directly (no reduction -- the
  // incoming chunk is already fully reduced). After N - 1 steps every rank
  // holds all N fully-reduced chunks.
  for (int step = 0; step < n - 1; ++step) {
    const int send_idx = ((rank - step + 1) % n + n) % n;
    const int recv_idx = ((rank - step) % n + n) % n;

    const std::size_t send_off = layout.offsets[static_cast<std::size_t>(send_idx)];
    const std::size_t send_size = layout.sizes[static_cast<std::size_t>(send_idx)];
    const std::size_t recv_off = layout.offsets[static_cast<std::size_t>(recv_idx)];
    const std::size_t recv_size = layout.sizes[static_cast<std::size_t>(recv_idx)];

    MPI_Request requests[2];
    MPI_Isend(mpi_buffer + send_off, detail::chunk_count_to_mpi_int(send_size), dtype, next,
               kAllGatherTag, comm, &requests[0]);
    MPI_Irecv(mpi_buffer + recv_off, detail::chunk_count_to_mpi_int(recv_size), dtype, prev,
               kAllGatherTag, comm, &requests[1]);
    MPI_Waitall(2, requests, MPI_STATUSES_IGNORE);
  }
}

}  // namespace ring_allreduce
