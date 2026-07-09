#pragma once

#include <mpi.h>

namespace ring_allreduce {

/// Thin wrapper around MPI_Wtime.
///
/// Deliberately built on MPI_Wtime rather than std::chrono: MPI_Wtime is
/// guaranteed monotonic within a single run and is the clock every MPI
/// implementation's own internal timing (and every paper this project cites)
/// uses, which keeps this project's numbers comparable to reference
/// benchmarks like OSU Micro-Benchmarks and nccl-tests. It is not
/// synchronized *across* independent mpirun invocations or wall-clock time
/// -- only elapsed differences within one run are meaningful.
///
/// Not thread-safe beyond what MPI_Wtime itself guarantees; this project
/// uses one OS thread per rank, so that is not a practical concern here.
class Timer {
 public:
  /// Records the current time as the start of a timed region.
  void start() { start_seconds_ = MPI_Wtime(); }

  /// Records the current time as the end of a timed region and returns the
  /// elapsed seconds since the matching start().
  double stop() {
    elapsed_seconds_ = MPI_Wtime() - start_seconds_;
    return elapsed_seconds_;
  }

  /// The elapsed seconds recorded by the most recent stop() call.
  double elapsed_seconds() const { return elapsed_seconds_; }

 private:
  double start_seconds_ = 0.0;
  double elapsed_seconds_ = 0.0;
};

}  // namespace ring_allreduce
