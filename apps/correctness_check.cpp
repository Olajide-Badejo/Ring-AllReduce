// Standalone, hand-runnable correctness driver.
//
// Usage: mpirun -np 8 ./correctness_check [count]
//
// Every rank seeds its buffer with (rank + 1) and allreduces with SumOp;
// the closed-form expected result is size*(size+1)/2 at every element, on
// every rank. This is deliberately simpler than the ctest suite (which
// checks against MPI_Allreduce and a Reduce+Bcast reference across the full
// count/dtype/op matrix) -- it exists so a human can sanity-check the build
// by hand in one line without reaching for ctest.

#include <mpi.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ring_allreduce/ring_allreduce.hpp"

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  const std::size_t count = (argc > 1) ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                                        : 1000;

  std::vector<float> buf(count);
  for (std::size_t i = 0; i < count; ++i) buf[i] = static_cast<float>(rank + 1);

  ring_allreduce::allreduce<float, ring_allreduce::SumOp>(count == 0 ? nullptr : buf.data(), count,
                                                           MPI_COMM_WORLD);

  const float expected = static_cast<float>(size) * static_cast<float>(size + 1) / 2.0f;
  bool ok = true;
  for (std::size_t i = 0; i < count; ++i) {
    if (buf[i] != expected) {
      ok = false;
      break;
    }
  }

  int local_ok = ok ? 1 : 0;
  int global_ok = 0;
  MPI_Allreduce(&local_ok, &global_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

  if (rank == 0) {
    std::printf("ring_allreduce correctness_check: N=%d count=%zu expected=%.1f -> %s\n", size,
                count, static_cast<double>(expected), global_ok ? "PASS" : "FAIL");
  }

  MPI_Finalize();
  return global_ok ? 0 : 1;
}
