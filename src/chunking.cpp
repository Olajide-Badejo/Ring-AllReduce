#include "ring_allreduce/chunking.hpp"

#include <cassert>

namespace ring_allreduce {

ChunkLayout compute_chunks(std::size_t count, std::size_t n) {
  assert(n >= 1 && "compute_chunks: n must be >= 1");

  ChunkLayout layout;
  layout.sizes.resize(n);
  layout.offsets.resize(n);

  const std::size_t base = count / n;
  const std::size_t remainder = count % n;

  std::size_t offset = 0;
  for (std::size_t i = 0; i < n; ++i) {
    // The first `remainder` chunks absorb the one-extra-element remainder,
    // per the spec: "Distribute any remainder so the first count % N chunks
    // get one extra element." This also naturally produces size-0 trailing
    // chunks when count < n (base == 0, i < remainder is false for i >= count).
    const std::size_t size = base + (i < remainder ? 1 : 0);
    layout.sizes[i] = size;
    layout.offsets[i] = offset;
    offset += size;
  }
  return layout;
}

}  // namespace ring_allreduce
