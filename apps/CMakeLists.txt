# apps/correctness_check: hand-runnable sanity driver (mpirun -np 8 ./correctness_check).
add_executable(correctness_check correctness_check.cpp)
target_link_libraries(correctness_check PRIVATE ring_allreduce::core)
ring_allreduce_apply_common_options(correctness_check)

# apps/benchmark: the main benchmark driver (see scripts/run_local_sweep.sh).
add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PRIVATE ring_allreduce::core)
ring_allreduce_apply_common_options(benchmark)

# apps/pingpong: raw MPI_Send/Recv latency reference (2 ranks only).
add_executable(pingpong pingpong.cpp)
target_link_libraries(pingpong PRIVATE MPI::MPI_CXX)
ring_allreduce_apply_common_options(pingpong)
