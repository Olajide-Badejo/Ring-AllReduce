#!/usr/bin/env python3
"""Generates results/sample_run/{results,pingpong}.csv -- SYNTHETIC DATA.

*** THIS DATA WAS NOT MEASURED ON REAL HARDWARE ***

The sandbox this repository was originally built in has no MPI
installation and no network access to install one (see
docs/DESIGN_DECISIONS.md), so apps/benchmark.cpp and apps/pingpong.cpp
could be written and reviewed but never actually executed there. Rather
than ship an empty results/ directory (which would leave
analysis/run_full_analysis.py and the LaTeX report with nothing to
demonstrate against), this script generates a clearly-labeled synthetic
dataset from the Hockney cost model (T = 2(N-1)*alpha + 2*(N-1)/N*bytes*beta)
with injected noise, so the analysis-and-report PIPELINE can be built,
run, and reviewed end to end.

REPLACE THIS DATA before citing any number from report/main.pdf as a real
finding: run `scripts/run_local_sweep.sh` (or the Slurm equivalent) on a
machine with a real MPI installation -- e.g. the i7-14700K workstation
this project's build spec targets -- and re-run
`analysis/run_full_analysis.py` against the real output. Every plot and
table this script's output feeds into carries the same disclaimer; see
results/sample_run/README.md.

The alpha/beta constants below are order-of-magnitude plausible for
single-node shared-memory MPI (microsecond-scale latency, single-digit-GB/s
bandwidth) -- chosen to make the pipeline's curve-fitting and plots produce
a sensible-*looking* ring-allreduce story (latency-bound at small messages,
bandwidth-bound at large ones), not fitted to, derived from, or claimed to
represent any specific real machine.
"""

import csv
import os

import numpy as np

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)))
RESULTS_CSV = os.path.join(OUT_DIR, "results.csv")
PINGPONG_CSV = os.path.join(OUT_DIR, "pingpong.csv")

N_VALUES = list(range(2, 17))
DTYPE = "float"
SIZES_BYTES = [1 << e for e in range(3, 28)]  # 8 .. 134,217,728
NUM_REPS = 20

# (alpha_seconds, beta_seconds_per_byte, noise_sigma) per algorithm. See
# module docstring: order-of-magnitude plausible, not measured, not fitted.
MODEL_PARAMS = {
    "ring": dict(alpha=3.0e-6, beta=1.20e-10, noise_sigma=0.06),
    "mpi-default": dict(alpha=2.0e-6, beta=1.15e-10, noise_sigma=0.05),
    "mpi-ring": dict(alpha=2.5e-6, beta=1.10e-10, noise_sigma=0.05),
}
PINGPONG_PARAMS = dict(alpha=1.5e-6, beta=1.00e-10, noise_sigma=0.04)

rng = np.random.default_rng(seed=42)


def hockney_mean_time(alpha, beta, n, size_bytes):
    if n <= 1:
        return 1e-9
    return 2.0 * (n - 1) * alpha + 2.0 * (n - 1) / n * size_bytes * beta


def synthetic_samples(mean_time, sigma, num_reps):
    # Multiplicative lognormal perturbation: right-skewed, like real wall
    # -clock measurements subject to occasional OS/scheduler jitter, rather
    # than symmetric Gaussian noise that could (unrealistically) go
    # negative.
    return mean_time * rng.lognormal(mean=0.0, sigma=sigma, size=num_reps)


def stats_from_samples(samples):
    s = np.sort(samples)
    return {
        "min": float(s[0]),
        "mean": float(np.mean(s)),
        "median": float(np.median(s)),
        "max": float(s[-1]),
        "stddev": float(np.std(s, ddof=1)) if len(s) > 1 else 0.0,
    }


def algbw_busbw(size_bytes, time_seconds, n):
    algbw = (size_bytes / time_seconds) / 1e9 if time_seconds > 0 else 0.0
    busbw_factor = 2.0 * (n - 1) / n if n > 1 else 0.0
    return algbw, algbw * busbw_factor


def generate_results_csv():
    with open(RESULTS_CSV, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow([
            "algorithm", "num_procs", "dtype", "message_size_bytes", "time_stat",
            "time_seconds", "algbw_GBps", "busbw_GBps", "num_reps",
        ])
        for algorithm, params in MODEL_PARAMS.items():
            for n in N_VALUES:
                for size_bytes in SIZES_BYTES:
                    mean_t = hockney_mean_time(params["alpha"], params["beta"], n, size_bytes)
                    samples = synthetic_samples(mean_t, params["noise_sigma"], NUM_REPS)
                    stats = stats_from_samples(samples)
                    for stat_name, t in stats.items():
                        algbw, busbw = algbw_busbw(size_bytes, t, n)
                        w.writerow([algorithm, n, DTYPE, size_bytes, stat_name, f"{t:.9e}",
                                    f"{algbw:.6f}", f"{busbw:.6f}", NUM_REPS])
    print(f"wrote {RESULTS_CSV}")


def generate_pingpong_csv():
    with open(PINGPONG_CSV, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["message_size_bytes", "time_stat", "time_seconds", "algbw_GBps", "num_reps"])
        for size_bytes in SIZES_BYTES:
            mean_t = PINGPONG_PARAMS["alpha"] + PINGPONG_PARAMS["beta"] * size_bytes
            samples = synthetic_samples(mean_t, PINGPONG_PARAMS["noise_sigma"], NUM_REPS)
            stats = stats_from_samples(samples)
            for stat_name, t in stats.items():
                algbw = (size_bytes / t) / 1e9 if t > 0 else 0.0
                w.writerow([size_bytes, stat_name, f"{t:.9e}", f"{algbw:.6f}", NUM_REPS])
    print(f"wrote {PINGPONG_CSV}")


if __name__ == "__main__":
    generate_results_csv()
    generate_pingpong_csv()
    print("\nThis is SYNTHETIC data (Hockney model + noise), not a real hardware measurement.")
    print("See results/sample_run/README.md before citing anything from it.")
