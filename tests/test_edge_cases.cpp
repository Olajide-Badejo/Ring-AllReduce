// Edge-case tests for ring_allreduce::allreduce<T, Op>, run under real MPI.
//
// Same sandbox caveat as test_ring_allreduce_correctness.cpp: written and
// ready to run via `mpirun -np N ./test_edge_cases` for each N in the
// matrix, but not executed in the environment this repository was built in
// (no MPI available there). See docs/DESIGN_DECISIONS.md.
//
// This file targets the specific failure modes Section 3 calls out by name
// rather than re-running the general count/op matrix (that is
// test_ring_allreduce_correctness.cpp's job):
//   - N == 1 as a genuine no-op, not just "happens to produce the right
//     number".
//   - count < N at the *exact* smallest message sizes the benchmark sweeps
//     (8-64 bytes), which is where most chunks are size 0 -- explicitly
//     flagged as "not hypothetical, it happens on your first real sweep".
//   - An exhaustive (not sampled) sweep of count from 0..N so the
//     size-0/size-1 boundary can't hide an off-by-one between test points.

#include <mpi.h>

#include <cstddef>
#include <vector>

#include "micro_test.hpp"
#include "ring_allreduce/ring_allreduce.hpp"

namespace {

int g_rank = -1;
int g_size = -1;

template <typename T>
T seed(int rank, std::size_t i) {
  // Same small-integer scheme as test_ring_allreduce_correctness.cpp: exact
  // under IEEE 754 regardless of reduction order, so equality checks below
  // are bit-exact rather than tolerance-based.
  return static_cast<T>((rank * 31 + static_cast<long long>(i) * 7) % 13);
}

template <typename T>
T expected_sum_at(std::size_t i) {
  T total = static_cast<T>(0);
  for (int r = 0; r < g_size; ++r) total = total + seed<T>(r, i);
  return total;
}

template <typename T>
void check_sum_allreduce(std::size_t count) {
  std::vector<T> buf(count);
  for (std::size_t i = 0; i < count; ++i) buf[i] = seed<T>(g_rank, i);
  ring_allreduce::allreduce<T, ring_allreduce::SumOp>(count == 0 ? nullptr : buf.data(), count,
                                                       MPI_COMM_WORLD);
  for (std::size_t i = 0; i < count; ++i) {
    REQUIRE(buf[i] == expected_sum_at<T>(i));
  }
}

}  // namespace

TEST_CASE("N == 1 is a true no-op: buffer is bit-identical to its input, for several counts") {
  if (g_size != 1) return;  // This test only means something under -np 1.
  for (std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{1024}}) {
    std::vector<float> original(count);
    for (std::size_t i = 0; i < count; ++i) original[i] = seed<float>(0, i);
    std::vector<float> buf = original;
    ring_allreduce::allreduce<float, ring_allreduce::SumOp>(count == 0 ? nullptr : buf.data(),
                                                            count, MPI_COMM_WORLD);
    for (std::size_t i = 0; i < count; ++i) REQUIRE(buf[i] == original[i]);
  }
}

TEST_CASE("count sweeps exhaustively from 0 to N: no off-by-one hides at the size-0/1 boundary") {
  // Sec. 3 flags this exact range as where most compute_chunks off-by-ones
  // surface, and specifically warns the failure "is not hypothetical" once
  // N=16 meets the smallest benchmark message sizes. Exhaustive, not
  // sampled: every count in [0, N] is checked, for both element types.
  for (std::size_t count = 0; count <= static_cast<std::size_t>(g_size); ++count) {
    check_sum_allreduce<float>(count);
    check_sum_allreduce<double>(count);
  }
}

TEST_CASE("smallest real benchmark sweep sizes (8B..64B) translated to element counts") {
  // The benchmark's message-size sweep starts at 8 bytes. Translate the
  // smallest few sizes into element counts for each dtype and run them for
  // real, at whatever (possibly large) N this binary was launched with --
  // this is precisely the "N=16 + smallest sizes -> mostly zero-size
  // chunks" scenario Sec. 3 calls out.
  for (std::size_t bytes : {std::size_t{8}, std::size_t{16}, std::size_t{32}, std::size_t{64}}) {
    check_sum_allreduce<float>(bytes / sizeof(float));
    check_sum_allreduce<double>(bytes / sizeof(double));
  }
}

TEST_CASE("repeated zero-count calls do not desync the ring (would hang, not misbehave, if broken)") {
  // A zero-byte MPI message is legal; the risk this guards against is an
  // implementation that special-cases size==0 by skipping a step's
  // Isend/Irecv/Waitall entirely, which would desync ranks whose neighbor
  // *did* still expect that round's handshake. If that were happening here,
  // this test would hang (and ctest's timeout would fail it), not silently
  // return a wrong value -- so the meaningful assertion is "this function
  // returns," repeated several times back to back.
  for (int iter = 0; iter < 5; ++iter) {
    ring_allreduce::allreduce<float, ring_allreduce::SumOp>(nullptr, 0, MPI_COMM_WORLD);
  }
  MPI_Barrier(MPI_COMM_WORLD);  // Every rank must reach here.
  REQUIRE(true);
}

TEST_CASE("non-power-of-two N sweeps the same exhaustive 0..N range as the power-of-two case") {
  // Sec. 2/3 both call out non-power-of-two N as first-class, not bolted
  // on; this duplicates the exhaustive 0..N sweep above but the point is
  // documentation and defense-in-depth, not new coverage -- it is cheap and
  // makes the intent explicit for whichever N this binary happens to run
  // under (mpirun -np 3, -np 5, -np 7 in the ctest matrix are all
  // non-power-of-two).
  for (std::size_t count = 0; count <= static_cast<std::size_t>(g_size); ++count) {
    check_sum_allreduce<double>(count);
  }
}

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &g_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &g_size);

  const int result = RUN_ALL_TESTS();

  int global_result = result;
  MPI_Allreduce(&result, &global_result, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  MPI_Finalize();
  return global_result;
}
