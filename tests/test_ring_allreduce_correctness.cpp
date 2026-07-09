// Correctness tests for ring_allreduce::allreduce<T, Op>, run under real MPI.
//
// *** CANNOT BE EXECUTED IN THE SANDBOX THIS REPOSITORY WAS BUILT IN ***
// That sandbox has no MPI installation and no network access to get one
// (see docs/DESIGN_DECISIONS.md). This file is written and structured
// exactly as it should be run -- `mpirun -np N ./test_ring_allreduce_correctness`
// for each N in the test matrix below -- but has not itself been executed
// there. The algorithm's index arithmetic (the part a transport-independent
// check *can* validate) was instead checked via
// tests/validate_ring_logic.py, which passed for all 8 N values and the
// full count/op matrix. Run this file for real, via `ctest`, before relying
// on it for anything -- that is precisely what Section 12's Definition of
// Done requires and what this comment is flagging has not yet happened.
//
// N is fixed per mpirun invocation (inherent to MPI), so this single binary
// internally sweeps the count and dtype/op matrix for whatever N it was
// launched with; tests/CMakeLists.txt registers one ctest entry per N.
//
// Two independent references are checked per Sec. 3's requirement that
// matching only MPI_Allreduce is not sufficient:
//   (a) vendor MPI_Allreduce (MPI_IN_PLACE, same op), and
//   (b) MPI_Reduce-to-root + MPI_Bcast, a *different* pair of collectives
//       computing the same logical answer via a different code path.
// All three must agree, elementwise, on every rank.
//
// Seed values are deliberately chosen so the reduction is exact in IEEE 754
// regardless of the order individual implementations combine elements in
// (see per-op comments below) -- this keeps the comparisons bit-exact
// instead of needing a tolerance, which would risk masking a real bug.

#include <mpi.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "micro_test.hpp"
#include "ring_allreduce/ring_allreduce.hpp"

namespace {

int g_rank = -1;
int g_size = -1;

// A "large" count for the full point-to-point pipeline: big enough to
// exercise multi-megabyte chunks and several send/recv steps worth of real
// data, small enough that N=16 ranks each allocating a handful of buffers
// this size cannot exhaust a typical dev or CI machine's RAM. The literal
// ">INT_MAX elements" scenario chunk_count_to_mpi_int() guards against is
// covered separately (and cheaply, with no real allocation) in
// test_chunking.cpp; see docs/DESIGN_DECISIONS.md for why it is not
// additionally re-tested here with a real multi-gigabyte buffer.
constexpr std::size_t kLargeCount = 4'194'304;  // 2^22

std::vector<std::size_t> count_matrix() {
  std::vector<std::size_t> counts = {
      0, 1, static_cast<std::size_t>(g_size > 1 ? g_size - 1 : 0),
      static_cast<std::size_t>(g_size), static_cast<std::size_t>(g_size) + 1, 1024, kLargeCount};
  std::sort(counts.begin(), counts.end());
  counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
  return counts;
}

// Sum: small nonnegative integers (mod 13) representable exactly as float
// or double, and whose partial sums across up to 16 ranks stay far below
// the exact-integer range of either format -- so ordinary IEEE 754 addition
// commutes and associates exactly here, regardless of summation order.
template <typename T>
T seed_sum(int rank, std::size_t i) {
  return static_cast<T>((rank * 31 + static_cast<long long>(i) * 7) % 13);
}

// Max/Min: order and grouping never change a max or a min, so any integer
// -valued seed works; kept in a modest signed range for readability.
template <typename T>
T seed_maxmin(int rank, std::size_t i) {
  return static_cast<T>((rank * 17 + static_cast<long long>(i) * 5) % 97 - 48);
}

// Prod: powers of two multiply exactly in IEEE 754 (pure exponent
// arithmetic, no rounding), so products stay bit-exact across reduction
// orders as long as the exponent range does not overflow -- kept small
// (2^-2 .. 2^2) for margin across up to 16 factors.
template <typename T>
T seed_prod(int rank, std::size_t i) {
  const int e = ((rank + static_cast<int>(i)) % 5) - 2;
  T v = static_cast<T>(1);
  if (e >= 0) {
    for (int k = 0; k < e; ++k) v *= static_cast<T>(2);
  } else {
    for (int k = 0; k < -e; ++k) v /= static_cast<T>(2);
  }
  return v;
}

template <typename T, typename Op>
void check_op(MPI_Datatype dtype, MPI_Op mpi_op, T (*seed)(int, std::size_t)) {
  for (std::size_t count : count_matrix()) {
    std::vector<T> ring_buf(count), mpi_buf(count), ref_buf(count), ref_root(count);
    for (std::size_t i = 0; i < count; ++i) {
      const T v = seed(g_rank, i);
      ring_buf[i] = v;
      mpi_buf[i] = v;
      ref_buf[i] = v;
    }

    ring_allreduce::allreduce<T, Op>(count == 0 ? nullptr : ring_buf.data(), count,
                                      MPI_COMM_WORLD);

    if (count > 0) {
      MPI_Allreduce(MPI_IN_PLACE, mpi_buf.data(), static_cast<int>(count), dtype, mpi_op,
                    MPI_COMM_WORLD);
      MPI_Reduce(ref_buf.data(), ref_root.data(), static_cast<int>(count), dtype, mpi_op, 0,
                 MPI_COMM_WORLD);
      MPI_Bcast(ref_root.data(), static_cast<int>(count), dtype, 0, MPI_COMM_WORLD);
    }

    for (std::size_t i = 0; i < count; ++i) {
      REQUIRE(ring_buf[i] == mpi_buf[i]);
      REQUIRE(ring_buf[i] == ref_root[i]);
    }
  }
}

}  // namespace

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: float, SumOp, full count matrix") {
  check_op<float, ring_allreduce::SumOp>(MPI_FLOAT, MPI_SUM, seed_sum<float>);
}

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: double, SumOp, full count matrix") {
  check_op<double, ring_allreduce::SumOp>(MPI_DOUBLE, MPI_SUM, seed_sum<double>);
}

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: float, MaxOp, full count matrix") {
  check_op<float, ring_allreduce::MaxOp>(MPI_FLOAT, MPI_MAX, seed_maxmin<float>);
}

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: double, MaxOp, full count matrix") {
  check_op<double, ring_allreduce::MaxOp>(MPI_DOUBLE, MPI_MAX, seed_maxmin<double>);
}

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: float, MinOp, full count matrix") {
  check_op<float, ring_allreduce::MinOp>(MPI_FLOAT, MPI_MIN, seed_maxmin<float>);
}

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: double, MinOp, full count matrix") {
  check_op<double, ring_allreduce::MinOp>(MPI_DOUBLE, MPI_MIN, seed_maxmin<double>);
}

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: float, ProdOp, full count matrix") {
  check_op<float, ring_allreduce::ProdOp>(MPI_FLOAT, MPI_PROD, seed_prod<float>);
}

TEST_CASE("ring_allreduce matches MPI_Allreduce and Reduce+Bcast: double, ProdOp, full count matrix") {
  check_op<double, ring_allreduce::ProdOp>(MPI_DOUBLE, MPI_PROD, seed_prod<double>);
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
