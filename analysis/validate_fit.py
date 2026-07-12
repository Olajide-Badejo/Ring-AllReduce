#!/usr/bin/env python3
"""validate_fit.py -- proves the alpha/beta fitting procedure is unbiased.

theoretical_model.fit_alpha_beta weights each row by 1/T rather than doing an
ordinary unweighted least-squares fit. That is a real methodological choice and
it needs evidence, not an assertion: this script is the evidence.

The problem it guards against. The message-size sweep spans 8 B to 128 MiB,
roughly seven orders of magnitude in T. An unweighted fit minimizes ABSOLUTE
squared error, so it is dominated by the handful of largest, slowest
configurations. Those pin down beta (the bandwidth term, which is what makes
them slow) very precisely, while alpha, whose entire contribution to T is only
visible at the smallest message sizes, is left almost unconstrained. The result
is a fit with an excellent-looking R^2 and a badly biased alpha, which would be
invisible if you only looked at goodness-of-fit.

Why this needs synthetic data, and cannot use the real measurements. To show a
fitter recovers the right answer, you must already know the right answer.
Real data does not come with its true alpha and beta attached; synthetic data
generated from known constants does. This is the same reason a numerical solver
is verified against a manufactured solution. Nothing here is used as, or
substitutes for, measured data: this script writes no files, and the dataset in
results/sample_run/ is a real Open MPI measurement.

Run it:  python3 analysis/validate_fit.py
Exits nonzero if the weighted fit fails to recover the known constants, so it
can be run as a check.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from theoretical_model import fit_alpha_beta, hockney_predict  # noqa: E402

# Known ground truth. Order-of-magnitude plausible for single-node shared-memory
# MPI, but the specific values do not matter: what is being tested is whether
# the fitter can recover whatever constants generated the data.
GROUND_TRUTH = {
    "case A": dict(alpha=3.0e-6, beta=1.20e-10, noise_sigma=0.06),
    "case B": dict(alpha=2.0e-6, beta=1.15e-10, noise_sigma=0.05),
    "case C": dict(alpha=2.5e-6, beta=1.10e-10, noise_sigma=0.05),
}

N_VALUES = list(range(2, 17))
SIZES_BYTES = [1 << e for e in range(3, 28)]  # 8 B .. 128 MiB, 25 sizes

# apps/benchmark.cpp times many repetitions per configuration and reports their
# median, and the analysis pipeline fits the median rows (ACHIEVED_STAT in
# generate_plots.py). So each synthetic point here is likewise the median of
# NUM_REPS noisy draws, not a single draw: fitting one raw sample per point
# would be testing the fitter against noisier input than it is ever given in
# production, and would understate how well it does.
NUM_REPS = 20

# The weighted fit must recover both constants to within this relative error.
TOLERANCE_PCT = 1.0


def fit_unweighted(n_values, size_values, time_values):
    """Ordinary least squares, no 1/T weighting. This is the naive approach
    whose failure mode this script exists to demonstrate."""
    n = np.asarray(n_values, dtype=float)
    m = np.asarray(size_values, dtype=float)
    t = np.asarray(time_values, dtype=float)
    x1 = 2.0 * (n - 1)
    x2 = np.where(n > 1, 2.0 * (n - 1) / n, 0.0) * m
    coeffs, *_ = np.linalg.lstsq(np.column_stack([x1, x2]), t, rcond=None)
    return float(coeffs[0]), float(coeffs[1])


def synthesize(alpha, beta, noise_sigma, rng):
    """Builds (N, M, T) triples from known constants, where each T is the
    median of NUM_REPS noisy repetitions, exactly as the real benchmark
    reports it.

    The noise is multiplicative lognormal: right-skewed like real wall-clock
    jitter, and never negative, which symmetric Gaussian noise could be."""
    n_out, m_out, t_out = [], [], []
    for n in N_VALUES:
        for size in SIZES_BYTES:
            mean_t = float(hockney_predict(n, size, alpha, beta))
            reps = mean_t * rng.lognormal(mean=0.0, sigma=noise_sigma, size=NUM_REPS)
            n_out.append(n)
            m_out.append(size)
            t_out.append(float(np.median(reps)))
    return np.array(n_out), np.array(m_out), np.array(t_out)


def pct_error(estimated, truth):
    return abs(estimated - truth) / truth * 100.0


def main():
    rng = np.random.default_rng(seed=42)
    print(f"Recovering known alpha/beta from synthetic data ({len(N_VALUES)} process counts "
          f"x {len(SIZES_BYTES)} sizes per case)\n")
    header = f"{'case':8} {'':>10} {'true':>12} {'unweighted':>12} {'err %':>8} " \
             f"{'weighted':>12} {'err %':>8}"
    print(header)
    print("-" * len(header))

    worst_weighted = 0.0
    worst_unweighted_alpha = 0.0

    for case, p in GROUND_TRUTH.items():
        n, m, t = synthesize(p["alpha"], p["beta"], p["noise_sigma"], rng)

        u_alpha, u_beta = fit_unweighted(n, m, t)
        w_alpha, w_beta, _r2 = fit_alpha_beta(n, m, t)

        rows = [
            ("alpha", p["alpha"], u_alpha, w_alpha, 1e6, "us"),
            ("beta", p["beta"], u_beta, w_beta, 1e9, "ns/B"),
        ]
        for name, truth, u_est, w_est, scale, unit in rows:
            u_err = pct_error(u_est, truth)
            w_err = pct_error(w_est, truth)
            print(f"{case:8} {name:>10} {truth * scale:>9.4f} {unit:<3}"
                  f"{u_est * scale:>9.4f}    {u_err:>7.2f} "
                  f"{w_est * scale:>9.4f}    {w_err:>7.2f}")
            worst_weighted = max(worst_weighted, w_err)
            if name == "alpha":
                worst_unweighted_alpha = max(worst_unweighted_alpha, u_err)
        print()

    print(f"Worst unweighted alpha error: {worst_unweighted_alpha:.1f}%   "
          f"(beta is recovered fine; alpha is what an unweighted fit loses)")
    print(f"Worst weighted error, either constant: {worst_weighted:.2f}%")

    if worst_weighted > TOLERANCE_PCT:
        print(f"\nFAIL: the 1/T-weighted fit missed a known constant by more than "
              f"{TOLERANCE_PCT}%.")
        return 1
    print(f"\nPASS: the 1/T-weighted fit recovered every known constant to within "
          f"{TOLERANCE_PCT}%.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
