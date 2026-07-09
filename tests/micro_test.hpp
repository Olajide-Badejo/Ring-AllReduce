#pragma once

// Minimal single-header test harness.
//
// WHY THIS EXISTS INSTEAD OF CATCH2 OR GOOGLETEST (the spec's own
// preference): this sandbox has no network access, Catch2's amalgamated
// header is not vendored anywhere on disk, and libgtest-dev is not
// installed and cannot be apt-installed offline. Rather than silently
// pretend to use one of them, this is a small, honest, from-scratch
// harness. See docs/DESIGN_DECISIONS.md for the full rationale and the
// (trivial) migration path once you have network access.
//
// API surface intentionally mirrors Catch2 so swapping later is close to a
// find-and-replace:
//   TEST_CASE("name")   like Catch2's TEST_CASE
//   REQUIRE(cond)        fatal:      aborts the current test case on failure
//   CHECK(cond)          non-fatal:  records failure, keeps running
//   RUN_ALL_TESTS()      like GoogleTest's RUN_ALL_TESTS(); call from main()

#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace micro_test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) {
    registry().push_back(TestCase{std::move(name), std::move(fn)});
  }
};

/// Failure counter for the *currently running* test case; reset before each
/// test case runs, inspected after it returns (or throws).
inline int& current_failures() {
  static int failures = 0;
  return failures;
}

/// Thrown by REQUIRE to unwind out of the current test case's body. Never
/// escapes RUN_ALL_TESTS(), which catches it per test case.
struct TestFailure {};

inline void report_failure(const char* kind, const char* expr, const char* file, int line) {
  std::fprintf(stderr, "      %s failed at %s:%d: %s\n", kind, file, line, expr);
  ++current_failures();
}

}  // namespace micro_test

#define MICRO_TEST_CONCAT_INNER(a, b) a##b
#define MICRO_TEST_CONCAT(a, b) MICRO_TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name_str)                                                              \
  static void MICRO_TEST_CONCAT(micro_test_fn_, __LINE__)();                             \
  namespace {                                                                            \
  ::micro_test::Registrar MICRO_TEST_CONCAT(micro_test_reg_, __LINE__)(                  \
      (name_str), &MICRO_TEST_CONCAT(micro_test_fn_, __LINE__));                         \
  }                                                                                       \
  static void MICRO_TEST_CONCAT(micro_test_fn_, __LINE__)()

#define CHECK(cond)                                                              \
  do {                                                                          \
    if (!(cond)) ::micro_test::report_failure("CHECK", #cond, __FILE__, __LINE__); \
  } while (0)

#define REQUIRE(cond)                                                             \
  do {                                                                           \
    if (!(cond)) {                                                              \
      ::micro_test::report_failure("REQUIRE", #cond, __FILE__, __LINE__);        \
      throw ::micro_test::TestFailure{};                                        \
    }                                                                            \
  } while (0)

/// Runs every TEST_CASE registered (via static initialization) in this
/// process, printing GoogleTest-style progress. Returns 0 iff every test
/// case passed, matching the exit-code convention ctest expects.
inline int RUN_ALL_TESTS() {
  int total = 0;
  int failed = 0;
  for (auto& t : micro_test::registry()) {
    ++total;
    micro_test::current_failures() = 0;
    std::printf("[ RUN      ] %s\n", t.name.c_str());
    try {
      t.fn();
    } catch (const micro_test::TestFailure&) {
      // Already reported by the REQUIRE that threw it.
    } catch (const std::exception& e) {
      micro_test::report_failure("EXCEPTION", e.what(), __FILE__, __LINE__);
    } catch (...) {
      micro_test::report_failure("EXCEPTION", "unknown exception", __FILE__, __LINE__);
    }
    if (micro_test::current_failures() > 0) {
      ++failed;
      std::printf("[  FAILED  ] %s\n", t.name.c_str());
    } else {
      std::printf("[       OK ] %s\n", t.name.c_str());
    }
  }
  std::printf("%d/%d test cases passed\n", total - failed, total);
  return failed == 0 ? 0 : 1;
}
