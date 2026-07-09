// apps/benchmark.cpp -- the main benchmark driver.
//
// N is fixed per mpirun invocation (inherent to MPI); this binary sweeps
// every message size and every requested --algorithm value internally in
// one launch, appending rows to a shared CSV (see scripts/run_local_sweep.sh,
// which loops only over process count). Do not launch a separate mpirun
// per message size -- that multiplies MPI startup/teardown overhead across
// the sweep and contaminates exactly the small-message latency numbers this
// project is trying to measure cleanly.
//
// "mpi-ring" note: forcing Open MPI's tuned collective to the ring
// algorithm happens via --mca flags on the mpirun *invocation itself*
// (MCA parameters are process-environment state read at MPI_Init, not
// something this program can flip mid-run), so --algorithm mpi-ring and
// --algorithm mpi-default execute the *identical* MPI_Allreduce call here;
// what differs is how the operator invoked mpirun (see
// scripts/run_local_sweep.sh and scripts/check_mpi_algorithms.sh). This
// binary does a best-effort runtime sanity check (not a hard failure) that
// the requested label matches what the OMPI_MCA_* environment looks like.
//
// CSV schema: algorithm,num_procs,dtype,message_size_bytes,time_stat,
// time_seconds,algbw_GBps,busbw_GBps,num_reps -- one row per (config,
// statistic) pair, i.e. 5 rows per (algorithm, N, dtype, size)
// configuration (min/mean/median/max/stddev), a "long" / tidy layout
// chosen so analysis/load_results.py can filter and pivot without any
// reshaping.

#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "ring_allreduce/ring_allreduce.hpp"

namespace {

struct BenchmarkConfig {
  std::vector<std::string> algorithms = {"ring", "mpi-default"};
  std::string dtype = "float";
  std::size_t min_bytes = 8;
  std::size_t max_bytes = 134217728;  // 128 MiB, 2^27
  int max_iterations = 1000;
  int warmup_iterations = 5;
  double max_time_per_config_ms = 300.0;
  std::string output_path = "results.csv";
};

std::vector<std::string> split_comma(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

void print_usage() {
  std::printf(
      "Usage: benchmark [options]\n"
      "  --algorithm <list>       comma-separated subset of {ring,mpi-default,mpi-ring}\n"
      "                           (default: ring,mpi-default)\n"
      "  --dtype {float,double}   element type (default: float)\n"
      "  --min-bytes <N>          smallest message size in bytes (default: 8)\n"
      "  --max-bytes <N>          largest message size in bytes (default: 134217728)\n"
      "  --iterations <N>         max timed reps per configuration (default: 1000)\n"
      "  --warmup-iterations <N>  untimed warmup reps per configuration (default: 5)\n"
      "  --max-time-per-config-ms <N>  wall-clock budget per configuration (default: 300)\n"
      "  --output <path>          CSV file to append rows to (default: results.csv)\n");
}

bool parse_args(int argc, char** argv, BenchmarkConfig& cfg, int rank) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) {
        if (rank == 0) std::fprintf(stderr, "missing value for %s\n", flag);
        std::exit(1);
      }
      return argv[++i];
    };
    if (arg == "--algorithm") {
      cfg.algorithms = split_comma(next("--algorithm"));
    } else if (arg == "--dtype") {
      cfg.dtype = next("--dtype");
    } else if (arg == "--min-bytes") {
      cfg.min_bytes = std::strtoull(next("--min-bytes").c_str(), nullptr, 10);
    } else if (arg == "--max-bytes") {
      cfg.max_bytes = std::strtoull(next("--max-bytes").c_str(), nullptr, 10);
    } else if (arg == "--iterations") {
      cfg.max_iterations = std::atoi(next("--iterations").c_str());
    } else if (arg == "--warmup-iterations") {
      cfg.warmup_iterations = std::atoi(next("--warmup-iterations").c_str());
    } else if (arg == "--max-time-per-config-ms") {
      cfg.max_time_per_config_ms = std::atof(next("--max-time-per-config-ms").c_str());
    } else if (arg == "--output") {
      cfg.output_path = next("--output");
    } else if (arg == "--help" || arg == "-h") {
      if (rank == 0) print_usage();
      return false;
    } else {
      if (rank == 0) {
        std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
        print_usage();
      }
      std::exit(1);
    }
  }
  return true;
}

std::vector<std::size_t> power_of_two_sizes(std::size_t min_bytes, std::size_t max_bytes) {
  std::vector<std::size_t> sizes;
  for (std::size_t s = 1; s <= max_bytes; s <<= 1) {
    if (s >= min_bytes) sizes.push_back(s);
  }
  return sizes;
}

struct TimingStats {
  double min_s, mean_s, median_s, max_s, stddev_s;
  int num_reps;
};

TimingStats compute_stats(std::vector<double> samples) {
  TimingStats st{};
  st.num_reps = static_cast<int>(samples.size());
  if (samples.empty()) return st;
  std::sort(samples.begin(), samples.end());
  st.min_s = samples.front();
  st.max_s = samples.back();
  const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  st.mean_s = sum / static_cast<double>(samples.size());
  const std::size_t mid = samples.size() / 2;
  st.median_s =
      (samples.size() % 2 == 0) ? (samples[mid - 1] + samples[mid]) / 2.0 : samples[mid];
  double sq_diff_sum = 0.0;
  for (double v : samples) sq_diff_sum += (v - st.mean_s) * (v - st.mean_s);
  st.stddev_s = samples.size() > 1 ? std::sqrt(sq_diff_sum / static_cast<double>(samples.size() - 1))
                                    : 0.0;
  return st;
}

void warn_if_mca_label_mismatch(const std::string& algorithm, int rank) {
  if (rank != 0) return;
  const char* dyn_rules = std::getenv("OMPI_MCA_coll_tuned_use_dynamic_rules");
  const char* algo_id = std::getenv("OMPI_MCA_coll_tuned_allreduce_algorithm");
  const bool mca_forcing_present = (dyn_rules != nullptr) && (algo_id != nullptr);
  if (algorithm == "mpi-ring" && !mca_forcing_present) {
    std::fprintf(stderr,
                 "warning: --algorithm mpi-ring requested but OMPI_MCA_coll_tuned_* forcing "
                 "env vars were not detected -- did you launch mpirun with --mca "
                 "coll_tuned_use_dynamic_rules 1 --mca coll_tuned_allreduce_algorithm <id>? "
                 "(see scripts/check_mpi_algorithms.sh). Rows will still be labeled "
                 "'mpi-ring' in the CSV, but may reflect the default algorithm instead.\n");
  }
  if (algorithm == "mpi-default" && mca_forcing_present) {
    std::fprintf(stderr,
                 "warning: --algorithm mpi-default requested but OMPI_MCA_coll_tuned_* forcing "
                 "env vars ARE present -- this mpirun invocation may have been launched with "
                 "ring-forcing flags meant for a separate 'mpi-ring' run.\n");
  }
}

// Runs one (algorithm, dtype T, count) configuration: warmup, then an
// adaptively-capped timed loop, Barrier/Wtime/Reduce-max per rep as
// specified in Sec. 5. Returns rank 0's TimingStats; other ranks' return
// value is unspecified (they only need to participate in the collectives).
template <typename T>
TimingStats run_one_config(const std::string& algorithm, std::size_t count,
                            const BenchmarkConfig& cfg, MPI_Comm comm, int rank) {
  std::vector<T> buf(std::max<std::size_t>(count, 1), static_cast<T>(rank + 1));
  const MPI_Datatype dtype = std::is_same<T, float>::value ? MPI_FLOAT : MPI_DOUBLE;

  auto do_op = [&]() {
    if (algorithm == "ring") {
      ring_allreduce::allreduce<T, ring_allreduce::SumOp>(count == 0 ? nullptr : buf.data(),
                                                           count, comm);
    } else {  // "mpi-default" or "mpi-ring": identical call, see file header note.
      if (count > 0) {
        MPI_Allreduce(MPI_IN_PLACE, buf.data(), static_cast<int>(count), dtype, MPI_SUM, comm);
      }
    }
  };

  for (int i = 0; i < cfg.warmup_iterations; ++i) {
    MPI_Barrier(comm);
    do_op();
  }
  MPI_Barrier(comm);

  std::vector<double> samples;  // meaningful on rank 0 only
  double elapsed_budget_s = 0.0;
  const double budget_s = cfg.max_time_per_config_ms / 1000.0;
  const int min_reps = 3;

  for (int rep = 0; rep < cfg.max_iterations; ++rep) {
    MPI_Barrier(comm);
    const double t0 = MPI_Wtime();
    do_op();
    const double t1 = MPI_Wtime();
    const double local_elapsed = t1 - t0;

    double max_elapsed = 0.0;
    MPI_Reduce(&local_elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, comm);

    int should_continue = 1;
    if (rank == 0) {
      samples.push_back(max_elapsed);
      elapsed_budget_s += max_elapsed;
      should_continue =
          (static_cast<int>(samples.size()) < min_reps) ||
          (elapsed_budget_s < budget_s && rep + 1 < cfg.max_iterations);
      should_continue = should_continue ? 1 : 0;
    }
    MPI_Bcast(&should_continue, 1, MPI_INT, 0, comm);
    if (!should_continue) break;
  }

  return rank == 0 ? compute_stats(samples) : TimingStats{};
}

void write_csv_row(std::ofstream& out, const std::string& algorithm, int num_procs,
                    const std::string& dtype, std::size_t message_size_bytes,
                    const std::string& time_stat, double time_seconds, int num_reps) {
  // busbw factor: 2*(N-1)/N, the bytes-moved-over-the-network-per-input-byte
  // ratio for any bandwidth-optimal point-to-point allreduce (Sec. 6.1).
  const double algbw_GBps =
      time_seconds > 0.0 ? (static_cast<double>(message_size_bytes) / time_seconds) / 1e9 : 0.0;
  const double busbw_factor =
      num_procs > 1 ? 2.0 * static_cast<double>(num_procs - 1) / static_cast<double>(num_procs)
                    : 0.0;
  const double busbw_GBps = algbw_GBps * busbw_factor;
  out << algorithm << ',' << num_procs << ',' << dtype << ',' << message_size_bytes << ','
      << time_stat << ',' << time_seconds << ',' << algbw_GBps << ',' << busbw_GBps << ','
      << num_reps << '\n';
}

template <typename T>
void run_sweep(const BenchmarkConfig& cfg, MPI_Comm comm, int rank, int size) {
  std::ofstream out;
  if (rank == 0) {
    const bool exists = static_cast<bool>(std::ifstream(cfg.output_path));
    out.open(cfg.output_path, std::ios::app);
    if (!exists) {
      out << "algorithm,num_procs,dtype,message_size_bytes,time_stat,time_seconds,algbw_GBps,"
             "busbw_GBps,num_reps\n";
    }
  }

  const auto sizes = power_of_two_sizes(cfg.min_bytes, cfg.max_bytes);
  for (std::size_t bytes : sizes) {
    const std::size_t count = bytes / sizeof(T);
    for (const auto& algorithm : cfg.algorithms) {
      warn_if_mca_label_mismatch(algorithm, rank);
      const TimingStats st = run_one_config<T>(algorithm, count, cfg, comm, rank);
      if (rank == 0) {
        write_csv_row(out, algorithm, size, cfg.dtype, bytes, "min", st.min_s, st.num_reps);
        write_csv_row(out, algorithm, size, cfg.dtype, bytes, "mean", st.mean_s, st.num_reps);
        write_csv_row(out, algorithm, size, cfg.dtype, bytes, "median", st.median_s, st.num_reps);
        write_csv_row(out, algorithm, size, cfg.dtype, bytes, "max", st.max_s, st.num_reps);
        write_csv_row(out, algorithm, size, cfg.dtype, bytes, "stddev", st.stddev_s, st.num_reps);
        out.flush();
        std::printf("[N=%2d] %-11s dtype=%-6s size=%10zu B  median=%.3e s  busbw=%.4f GB/s\n",
                    size, algorithm.c_str(), cfg.dtype.c_str(), bytes, st.median_s,
                    st.median_s > 0.0 ? (static_cast<double>(bytes) / st.median_s / 1e9) *
                                            (size > 1 ? 2.0 * (size - 1) / size : 0.0)
                                      : 0.0);
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  BenchmarkConfig cfg;
  if (!parse_args(argc, argv, cfg, rank)) {
    MPI_Finalize();
    return 0;
  }

  if (cfg.dtype == "float") {
    run_sweep<float>(cfg, MPI_COMM_WORLD, rank, size);
  } else if (cfg.dtype == "double") {
    run_sweep<double>(cfg, MPI_COMM_WORLD, rank, size);
  } else {
    if (rank == 0) std::fprintf(stderr, "unknown --dtype '%s' (expected float or double)\n",
                                cfg.dtype.c_str());
    MPI_Finalize();
    return 1;
  }

  MPI_Finalize();
  return 0;
}
