#pragma once

#include <cstddef>
#include <vector>

namespace ring_allreduce {

/// The per-rank chunk layout of a buffer of `count` elements split across
/// `n` ranks for a ring collective.
///
/// `sizes[i]` and `offsets[i]` describe chunk `i`'s length and starting
/// element offset into the original buffer, for `i` in `[0, n)`. Chunks are
/// contiguous and cover the whole buffer exactly once:
/// `offsets[0] == 0`, `offsets[i+1] == offsets[i] + sizes[i]`, and
/// `offsets[n-1] + sizes[n-1] == count`.
struct ChunkLayout {
  std::vector<std::size_t> sizes;
  std::vector<std::size_t> offsets;
};

/// Splits a buffer of `count` elements into `n` chunks as evenly as
/// possible.
///
/// When `count % n != 0`, the first `count % n` chunks receive one extra
/// element each, so chunk sizes differ by at most one element. When
/// `count < n`, the trailing `n - count` chunks have size 0 (this is a
/// legal, first-class case: it happens routinely for small messages at
/// high process counts and must not be treated as an error).
///
/// Preconditions: `n >= 1`.
///
/// @param count total number of elements to distribute.
/// @param n     number of chunks (ranks) to distribute across.
/// @return      a ChunkLayout with `n` entries in both `sizes` and `offsets`.
ChunkLayout compute_chunks(std::size_t count, std::size_t n);

}  // namespace ring_allreduce
