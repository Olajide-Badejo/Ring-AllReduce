// Unit tests for ring_allreduce::compute_chunks and the reduce_ops functors.
//
// Deliberately MPI-free: neither compute_chunks nor the functors in
// reduce_ops.hpp touch MPI at all, so this file (unlike
// test_ring_allreduce_correctness.cpp and test_edge_cases.cpp) builds and
// runs as an ordinary host binary with no mpirun involved. reduce_ops
// functor checks live here rather than in their own file because they are
// the only other MPI-free piece of this library; the spec's file list has
// no dedicated file for them.

#include "micro_test.hpp"
#include "ring_allreduce/chunking.hpp"
#include "ring_allreduce/reduce_ops.hpp"

using ring_allreduce::ChunkLayout;
using ring_allreduce::compute_chunks;

namespace {

// Generic invariants every valid ChunkLayout must satisfy, checked against
// the documented remainder rule directly (not just "sizes sum to count") so
// a systematic off-by-one in compute_chunks cannot slip past a check that
// only verifies the total.
void check_layout(std::size_t count, std::size_t n, const ChunkLayout& layout) {
  REQUIRE(layout.sizes.size() == n);
  REQUIRE(layout.offsets.size() == n);

  const std::size_t base = count / n;
  const std::size_t remainder = count % n;
  std::size_t expected_offset = 0;
  std::size_t sum = 0;

  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t expected_size = base + (i < remainder ? 1 : 0);
    CHECK(layout.sizes[i] == expected_size);
    CHECK(layout.offsets[i] == expected_offset);
    expected_offset += expected_size;
    sum += layout.sizes[i];
  }
  CHECK(sum == count);
  CHECK(expected_offset == count);
}

}  // namespace

TEST_CASE("compute_chunks: even split, count divisible by n") {
  const auto layout = compute_chunks(8, 4);
  check_layout(8, 4, layout);
  // Spot-check the exact literal array too, independent of the generic
  // formula-based checker above.
  REQUIRE(layout.sizes.size() == 4);
  CHECK(layout.sizes[0] == 2);
  CHECK(layout.sizes[1] == 2);
  CHECK(layout.sizes[2] == 2);
  CHECK(layout.sizes[3] == 2);
  CHECK(layout.offsets[0] == 0);
  CHECK(layout.offsets[1] == 2);
  CHECK(layout.offsets[2] == 4);
  CHECK(layout.offsets[3] == 6);
}

TEST_CASE("compute_chunks: remainder goes to the first count%n chunks") {
  // count=10, n=4: base=2, remainder=2 -> sizes [3, 3, 2, 2].
  const auto layout = compute_chunks(10, 4);
  check_layout(10, 4, layout);
  REQUIRE(layout.sizes.size() == 4);
  CHECK(layout.sizes[0] == 3);
  CHECK(layout.sizes[1] == 3);
  CHECK(layout.sizes[2] == 2);
  CHECK(layout.sizes[3] == 2);
  CHECK(layout.offsets[0] == 0);
  CHECK(layout.offsets[1] == 3);
  CHECK(layout.offsets[2] == 6);
  CHECK(layout.offsets[3] == 8);
}

TEST_CASE("compute_chunks: count < n produces legal zero-size trailing chunks") {
  // count=3, n=8: base=0, remainder=3 -> sizes [1,1,1,0,0,0,0,0].
  const auto layout = compute_chunks(3, 8);
  check_layout(3, 8, layout);
  REQUIRE(layout.sizes.size() == 8);
  for (std::size_t i = 0; i < 3; ++i) CHECK(layout.sizes[i] == 1);
  for (std::size_t i = 3; i < 8; ++i) CHECK(layout.sizes[i] == 0);
  // Every zero-size chunk's offset must equal count (there is nowhere else
  // for an empty chunk to "start"), not some stale or garbage value.
  for (std::size_t i = 3; i < 8; ++i) CHECK(layout.offsets[i] == 3);
}

TEST_CASE("compute_chunks: count == 0 -> every chunk is size 0") {
  const auto layout = compute_chunks(0, 8);
  check_layout(0, 8, layout);
  for (std::size_t i = 0; i < 8; ++i) {
    CHECK(layout.sizes[i] == 0);
    CHECK(layout.offsets[i] == 0);
  }
}

TEST_CASE("compute_chunks: n == 1 -> a single chunk holding everything") {
  const auto layout = compute_chunks(12345, 1);
  check_layout(12345, 1, layout);
  REQUIRE(layout.sizes.size() == 1);
  CHECK(layout.sizes[0] == 12345);
  CHECK(layout.offsets[0] == 0);
}

TEST_CASE("compute_chunks: count == 1, n == 1 (the smallest legal case)") {
  const auto layout = compute_chunks(1, 1);
  check_layout(1, 1, layout);
  CHECK(layout.sizes[0] == 1);
  CHECK(layout.offsets[0] == 0);
}

TEST_CASE("compute_chunks: non-power-of-two n") {
  // count=17, n=5: base=3, remainder=2 -> sizes [4,4,3,3,3].
  const auto layout = compute_chunks(17, 5);
  check_layout(17, 5, layout);
  CHECK(layout.sizes[0] == 4);
  CHECK(layout.sizes[1] == 4);
  CHECK(layout.sizes[2] == 3);
  CHECK(layout.sizes[3] == 3);
  CHECK(layout.sizes[4] == 3);
}

TEST_CASE("compute_chunks: n == 7 (the full non-power-of-two N from the correctness matrix)") {
  for (std::size_t count : {0UL, 1UL, 6UL, 7UL, 8UL, 1024UL}) {
    check_layout(count, 7, compute_chunks(count, 7));
  }
}

TEST_CASE("compute_chunks: full N matrix from the correctness test plan, count == 1024") {
  for (std::size_t n : {1UL, 2UL, 3UL, 4UL, 5UL, 7UL, 8UL, 16UL}) {
    check_layout(1024, n, compute_chunks(1024, n));
  }
}

TEST_CASE("compute_chunks: count == N-1, N, N+1 for every N in the correctness test plan") {
  for (std::size_t n : {2UL, 3UL, 4UL, 5UL, 7UL, 8UL, 16UL}) {
    check_layout(n - 1, n, compute_chunks(n - 1, n));
    check_layout(n, n, compute_chunks(n, n));
    check_layout(n + 1, n, compute_chunks(n + 1, n));
  }
}

TEST_CASE("compute_chunks: a count spanning multiple 32-bit ints") {
  // 6,000,000,000 exceeds INT_MAX (~2.147 billion). compute_chunks itself
  // only manipulates small (size N) vectors of size_t -- no buffer of this
  // size is ever allocated -- so this validates the *arithmetic* is 64-bit
  // safe and correct at this scale without needing tens of GB of RAM.
  // (The point-to-point layer's own INT_MAX guard, which matters once real
  // buffers are involved, is exercised separately -- see
  // docs/DESIGN_DECISIONS.md for why that is not additionally stress-tested
  // with a real multi-GB allocation here.)
  const std::size_t huge_count = 6'000'000'000ULL;
  for (std::size_t n : {1UL, 3UL, 16UL}) {
    check_layout(huge_count, n, compute_chunks(huge_count, n));
  }
}

// --- reduce_ops functors -----------------------------------------------

TEST_CASE("SumOp: elementwise addition, float and double") {
  ring_allreduce::SumOp op;
  CHECK(op(2.0f, 3.0f) == 5.0f);
  CHECK(op(-1.5, 4.5) == 3.0);
  CHECK(op(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("MaxOp: elementwise maximum, float and double") {
  ring_allreduce::MaxOp op;
  CHECK(op(2.0f, 3.0f) == 3.0f);
  CHECK(op(3.0f, 2.0f) == 3.0f);
  CHECK(op(-5.0, -1.0) == -1.0);
}

TEST_CASE("MinOp: elementwise minimum, float and double") {
  ring_allreduce::MinOp op;
  CHECK(op(2.0f, 3.0f) == 2.0f);
  CHECK(op(3.0f, 2.0f) == 2.0f);
  CHECK(op(-5.0, -1.0) == -5.0);
}

TEST_CASE("ProdOp: elementwise product, float and double") {
  ring_allreduce::ProdOp op;
  CHECK(op(2.0f, 3.0f) == 6.0f);
  CHECK(op(-2.0, 4.0) == -8.0);
  CHECK(op(0.5f, 4.0f) == 2.0f);
}
