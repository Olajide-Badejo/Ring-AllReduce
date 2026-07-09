#include "ring_allreduce/ring_allreduce.hpp"

namespace ring_allreduce::detail {

std::pair<int, int> ring_neighbors(int rank, int n) {
  const int next = (rank + 1) % n;
  const int prev = (rank - 1 + n) % n;
  return {next, prev};
}

}  // namespace ring_allreduce::detail

// Explicit instantiation of every (T, Op) combination this repository's
// tests, apps/correctness_check, and apps/benchmark use.
//
// This is NOT required for correctness: allreduce<T, Op> is fully defined
// in the header, so any caller can implicitly instantiate it for any T/Op
// it likes. It is here so that (a) this library's own build typechecks the
// template body immediately rather than deferring that to whatever
// translation unit happens to instantiate it first, and (b) the float and
// double SumOp fast path -- the one every benchmark run actually takes --
// is compiled once, here, instead of redundantly in every caller.
namespace ring_allreduce {

template void allreduce<float, SumOp>(float*, std::size_t, MPI_Comm);
template void allreduce<double, SumOp>(double*, std::size_t, MPI_Comm);
template void allreduce<float, MaxOp>(float*, std::size_t, MPI_Comm);
template void allreduce<double, MaxOp>(double*, std::size_t, MPI_Comm);
template void allreduce<float, MinOp>(float*, std::size_t, MPI_Comm);
template void allreduce<double, MinOp>(double*, std::size_t, MPI_Comm);
template void allreduce<float, ProdOp>(float*, std::size_t, MPI_Comm);
template void allreduce<double, ProdOp>(double*, std::size_t, MPI_Comm);

}  // namespace ring_allreduce
