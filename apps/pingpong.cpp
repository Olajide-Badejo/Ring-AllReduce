// apps/pingpong.cpp -- raw point-to-point MPI_Send/Recv latency reference.
//
// Exists solely to answer one question the report's discussion depends on
// (Sec. 6.5): is the ring's fitted per-step alpha close to this hardware's
// *raw* point-to-point latency, or meaningfully larger? If the ring's alpha
// is bigger, that gap quantifies rank-synchronization overhead (stragglers,
// OS jitter, core contention) instead of leaving it as an assertion.
//
// Requires exactly 2 ranks: `mpirun -np 2 ./pingpong [--min-bytes N]
// [--max-bytes N] [--output path]`. Rank 0 sends, rank 1 echoes back;
// elapsed round-trip / 2 is recorded as a one-way-latency estimate, which is
// exactly what OSU Micro-Benchmarks' osu_latency reports and is the
// reference this project's numbers are meant to be comparable to.

#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

namespace {

struct PingpongConfig {
  std::size_t min_bytes = 8;
  std::size_t max_bytes = 134217728;
  int max_iterations = 1000;
  int warmup_iterations = 10;
  double max_time_per_config_ms = 200.0;
  std::string output_path = "pingpong.csv";
};

bool parse_args(int argc, char** argv, PingpongConfig& cfg, int rank) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) std::exit(1);
      return argv[++i];
    };
    if (arg == "--min-bytes") {
      cfg.min_bytes = std::strtoull(next().c_str(), nullptr, 10);
    } else if (arg == "--max-bytes") {
      cfg.max_bytes = std::strtoull(next().c_str(), nullptr, 10);
    } else if (arg == "--iterations") {
      cfg.max_iterations = std::atoi(next().c_str());
    } else if (arg == "--warmup-iterations") {
      cfg.warmup_iterations = std::atoi(next().c_str());
    } else if (arg == "--max-time-per-config-ms") {
      cfg.max_time_per_config_ms = std::atof(next().c_str());
    } else if (arg == "--output") {
      cfg.output_path = next();
    } else if (arg == "--help" || arg == "-h") {
      if (rank == 0) {
        std::printf(
            "Usage: pingpong [--min-bytes N] [--max-bytes N] [--iterations N] "
            "[--warmup-iterations N] [--max-time-per-config-ms N] [--output path]\n");
      }
      return false;
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
  double min_s = 0, mean_s = 0, median_s = 0, max_s = 0, stddev_s = 0;
  int num_reps = 0;
};

TimingStats compute_stats(std::vector<double> samples) {
  TimingStats st;
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
  double sq = 0.0;
  for (double v : samples) sq += (v - st.mean_s) * (v - st.mean_s);
  st.stddev_s = samples.size() > 1 ? std::sqrt(sq / static_cast<double>(samples.size() - 1)) : 0.0;
  return st;
}

void write_row(std::ofstream& out, std::size_t bytes, const std::string& stat, double seconds,
               int reps) {
  const double algbw_GBps = seconds > 0.0 ? (static_cast<double>(bytes) / seconds) / 1e9 : 0.0;
  out << bytes << ',' << stat << ',' << seconds << ',' << algbw_GBps << ',' << reps << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (size != 2) {
    if (rank == 0) {
      std::fprintf(stderr, "pingpong requires exactly 2 ranks (got %d); run: mpirun -np 2 ./pingpong\n",
                   size);
    }
    MPI_Finalize();
    return 1;
  }

  PingpongConfig cfg;
  if (!parse_args(argc, argv, cfg, rank)) {
    MPI_Finalize();
    return 0;
  }

  std::ofstream out;
  if (rank == 0) {
    const bool exists = static_cast<bool>(std::ifstream(cfg.output_path));
    out.open(cfg.output_path, std::ios::app);
    if (!exists) out << "message_size_bytes,time_stat,time_seconds,algbw_GBps,num_reps\n";
  }

  for (std::size_t bytes : power_of_two_sizes(cfg.min_bytes, cfg.max_bytes)) {
    std::vector<char> buf(bytes, 'x');

    auto do_pingpong = [&]() {
      if (rank == 0) {
        MPI_Send(buf.data(), static_cast<int>(bytes), MPI_CHAR, 1, 0, MPI_COMM_WORLD);
        MPI_Recv(buf.data(), static_cast<int>(bytes), MPI_CHAR, 1, 0, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
      } else {
        MPI_Recv(buf.data(), static_cast<int>(bytes), MPI_CHAR, 0, 0, MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        MPI_Send(buf.data(), static_cast<int>(bytes), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
      }
    };

    for (int i = 0; i < cfg.warmup_iterations; ++i) {
      MPI_Barrier(MPI_COMM_WORLD);
      do_pingpong();
    }
    MPI_Barrier(MPI_COMM_WORLD);

    std::vector<double> one_way_samples;  // rank 0 only
    double budget_used_s = 0.0;
    const double budget_s = cfg.max_time_per_config_ms / 1000.0;
    const int min_reps = 5;

    for (int rep = 0; rep < cfg.max_iterations; ++rep) {
      MPI_Barrier(MPI_COMM_WORLD);
      const double t0 = MPI_Wtime();
      do_pingpong();
      const double t1 = MPI_Wtime();

      int should_continue = 1;
      if (rank == 0) {
        const double round_trip = t1 - t0;
        one_way_samples.push_back(round_trip / 2.0);
        budget_used_s += round_trip;
        should_continue = (static_cast<int>(one_way_samples.size()) < min_reps) ||
                          (budget_used_s < budget_s && rep + 1 < cfg.max_iterations);
      }
      MPI_Bcast(&should_continue, 1, MPI_INT, 0, MPI_COMM_WORLD);
      if (!should_continue) break;
    }

    if (rank == 0) {
      const TimingStats st = compute_stats(one_way_samples);
      write_row(out, bytes, "min", st.min_s, st.num_reps);
      write_row(out, bytes, "mean", st.mean_s, st.num_reps);
      write_row(out, bytes, "median", st.median_s, st.num_reps);
      write_row(out, bytes, "max", st.max_s, st.num_reps);
      write_row(out, bytes, "stddev", st.stddev_s, st.num_reps);
      out.flush();
      std::printf("size=%10zu B  one-way median=%.3e s\n", bytes, st.median_s);
    }
  }

  MPI_Finalize();
  return 0;
}
