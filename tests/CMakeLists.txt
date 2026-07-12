# test_chunking: no MPI communication happens in compute_chunks or the
# reduce_ops functors, so this runs as an ordinary host binary -- a single
# ctest entry, no mpirun involved.
add_executable(test_chunking test_chunking.cpp test_main.cpp)
target_link_libraries(test_chunking PRIVATE ring_allreduce::core)
target_include_directories(test_chunking PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
ring_allreduce_apply_common_options(test_chunking)
add_test(NAME test_chunking COMMAND test_chunking)

# test_ring_allreduce_correctness / test_edge_cases: N is fixed per mpirun
# invocation (inherent to MPI, see apps/benchmark.cpp for the same
# constraint), so each binary internally sweeps the count/dtype/op matrix
# for whatever N it is launched with, and one ctest entry is registered per
# N in Sec. 3's test matrix (non-power-of-two N is mandatory, not optional).
#
# Open MPI on the Linux CI runner needs --allow-run-as-root and
# --oversubscribe for the N=16 case. Microsoft MPI uses a different launcher
# grammar and correctly rejects those flags, so do not pass them on Windows.
set(RING_ALLREDUCE_MPIEXEC_EXTRA_FLAGS)
if(UNIX)
  list(APPEND RING_ALLREDUCE_MPIEXEC_EXTRA_FLAGS --allow-run-as-root --oversubscribe)
endif()

set(RING_ALLREDUCE_TEST_N_VALUES 1 2 3 4 5 7 8 16)

foreach(test_name test_ring_allreduce_correctness test_edge_cases)
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE ring_allreduce::core)
  target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  ring_allreduce_apply_common_options(${test_name})

  foreach(n ${RING_ALLREDUCE_TEST_N_VALUES})
    add_test(
      NAME ${test_name}_np${n}
      COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${n} ${MPIEXEC_PREFLAGS}
              ${RING_ALLREDUCE_MPIEXEC_EXTRA_FLAGS}
              $<TARGET_FILE:${test_name}>
              ${MPIEXEC_POSTFLAGS}
    )
  endforeach()
endforeach()
