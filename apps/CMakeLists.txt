# apps/correctness_check: hand-runnable sanity driver (mpirun -np 8 ./correctness_check).
# Fleshed out in Phase 2 once the real algorithm exists; for now (Phase 1 scaffold)
# it is an MPI init/finalize stub that proves the build+link wiring works end to end.
add_executable(correctness_check correctness_check.cpp)
target_link_libraries(correctness_check PRIVATE ring_allreduce::core)
ring_allreduce_apply_common_options(correctness_check)

# apps/benchmark and apps/pingpong are added in Phase 3 once their sources exist.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark.cpp")
  add_executable(benchmark benchmark.cpp)
  target_link_libraries(benchmark PRIVATE ring_allreduce::core)
  ring_allreduce_apply_common_options(benchmark)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pingpong.cpp")
  add_executable(pingpong pingpong.cpp)
  target_link_libraries(pingpong PRIVATE MPI::MPI_CXX)
  ring_allreduce_apply_common_options(pingpong)
endif()
