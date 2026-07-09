#pragma once

namespace ring_allreduce {

/// Elementwise sum: `op(a, b) = a + b`. This is the default `Op` for
/// allreduce<T, Op> and the only operator this project's benchmark actually
/// measures (see README.md and report/sections/04_experimental_setup.tex).
struct SumOp {
  template <typename T>
  T operator()(T a, T b) const {
    return a + b;
  }
};

/// Elementwise maximum: `op(a, b) = a > b ? a : b`.
struct MaxOp {
  template <typename T>
  T operator()(T a, T b) const {
    return a > b ? a : b;
  }
};

/// Elementwise minimum: `op(a, b) = a < b ? a : b`.
struct MinOp {
  template <typename T>
  T operator()(T a, T b) const {
    return a < b ? a : b;
  }
};

/// Elementwise product: `op(a, b) = a * b`.
struct ProdOp {
  template <typename T>
  T operator()(T a, T b) const {
    return a * b;
  }
};

}  // namespace ring_allreduce
