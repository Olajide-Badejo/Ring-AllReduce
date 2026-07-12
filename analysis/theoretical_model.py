"""theoretical_model.py -- Hockney alpha-beta cost model for ring-allreduce.

    T(N, M) ~= 2(N-1)*alpha + 2*((N-1)/N)*M*beta

alpha = per-message latency (seconds), beta = inverse bandwidth
(seconds/byte). Per Sec. 6.2, alpha and beta are fit from this project's OWN
measured (N, M, T) data via least squares, never hardcoded from a textbook
or another machine.
"""

import numpy as np


def hockney_predict(n, size_bytes, alpha, beta):
    """Predicts T(N, M) for scalar or array-like n / size_bytes."""
    n = np.asarray(n, dtype=float)
    size_bytes = np.asarray(size_bytes, dtype=float)
    busbw_factor = np.where(n > 1, 2.0 * (n - 1) / n, 0.0)
    return 2.0 * (n - 1) * alpha + busbw_factor * size_bytes * beta


def fit_alpha_beta(n_values, size_values, time_values):
    """Weighted least-squares fit of T = 2(N-1)*alpha + 2*((N-1)/N)*M*beta.

    The message-size sweep spans 8 bytes to 128 MiB, about seven orders of
    magnitude in size and therefore in T. An UNWEIGHTED least-squares fit minimizes absolute
    squared error, which is completely dominated by the largest handful of
    message sizes (their T values, and hence their squared errors, are
    enormously larger in absolute terms than the small-message points).
    That fits beta (the bandwidth term, which dominates T at large M)
    precisely, but alpha's contribution to T only matters at small M, which
    then carries almost no weight in the unweighted sum of squares -- so
    alpha comes out noisy and can be badly biased even when R^2 looks
    excellent.

    The fix used here is to weight each row by 1/T, turning the fit into a
    relative-error (not absolute-error) minimization: every message size
    contributes comparably regardless of its absolute magnitude. This assumes
    measurement noise has roughly constant coefficient of variation across
    scales, a standard and defensible assumption for wall-clock timing noise.

    This is not asserted, it is checked. analysis/validate_fit.py recovers
    known alpha/beta from synthetic data (the only kind that has a known
    ground truth to recover): unweighted OLS misses alpha by up to 17% while
    getting beta right, and the 1/T-weighted fit below recovers both to
    within 0.4%. Run it to reproduce; see docs/DESIGN_DECISIONS.md.

    Args:
      n_values: array-like of process counts N for each measurement.
      size_values: array-like of message sizes in bytes for each measurement.
      time_values: array-like of measured times in seconds for each
        measurement (same length as n_values/size_values; must be > 0).

    Returns:
      (alpha_seconds, beta_seconds_per_byte, r_squared). r_squared is
      computed on the original (unweighted) residuals, so it remains an
      ordinary goodness-of-fit number even though the fit itself is
      weighted.
    """
    n = np.asarray(n_values, dtype=float)
    m = np.asarray(size_values, dtype=float)
    t = np.asarray(time_values, dtype=float)
    if not (len(n) == len(m) == len(t)):
        raise ValueError("fit_alpha_beta: n_values, size_values, time_values must be equal length")
    if len(n) < 2:
        raise ValueError("fit_alpha_beta: need at least 2 data points to fit 2 parameters")
    if np.any(t <= 0):
        raise ValueError("fit_alpha_beta: time_values must be strictly positive to weight by 1/T")

    x1 = 2.0 * (n - 1)
    x2 = np.where(n > 1, 2.0 * (n - 1) / n, 0.0) * m
    design_matrix = np.column_stack([x1, x2])

    weights = 1.0 / t
    weighted_design = design_matrix * weights[:, np.newaxis]
    weighted_target = t * weights

    coeffs, _residuals, _rank, _singular_values = np.linalg.lstsq(
        weighted_design, weighted_target, rcond=None
    )
    alpha, beta = coeffs

    predicted = design_matrix @ coeffs
    ss_res = float(np.sum((t - predicted) ** 2))
    ss_tot = float(np.sum((t - np.mean(t)) ** 2))
    r_squared = 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")

    return float(alpha), float(beta), r_squared


def theoretical_peak_busbw_GBps(beta_seconds_per_byte):
    """Theoretical peak bus bandwidth 1/beta, in GB/s.

    beta is seconds/byte, so 1/beta is bytes/second; dividing by 1e9
    matches this project's GB/s convention (decimal, 1e9 bytes = 1 GB),
    the same convention apps/benchmark.cpp uses for algbw_GBps/busbw_GBps.
    """
    if beta_seconds_per_byte <= 0:
        return float("inf")
    return (1.0 / beta_seconds_per_byte) / 1e9


def efficiency(achieved_busbw_GBps, theoretical_peak_GBps):
    """achieved / theoretical, in [0, 1] for a physically sensible fit."""
    if not np.isfinite(theoretical_peak_GBps) or theoretical_peak_GBps <= 0:
        return float("nan")
    return achieved_busbw_GBps / theoretical_peak_GBps


def step_count_table(n_values):
    """Builds the Sec. 6.3 step-count comparison: ring, recursive doubling,
    Rabenseifner, for each N in n_values.

    Ring: 2(N-1) steps (reduce-scatter + all-gather, N-1 each).
    Recursive doubling: ceil(log2(N)) steps (the classic power-of-two
      allreduce; used here for any N, power-of-two or not, as the standard
      reference point Sec. 6.3 asks for).
    Rabenseifner: 2*ceil(log2(N)) steps (recursive-halving reduce-scatter +
      recursive-doubling all-gather).

    Returns a list of dicts: [{"N": n, "ring": ..., "recursive_doubling":
    ..., "rabenseifner": ...}, ...].
    """
    rows = []
    for n in n_values:
        if n < 1:
            raise ValueError("step_count_table: N must be >= 1")
        log2n = int(np.ceil(np.log2(n))) if n > 1 else 0
        rows.append({
            "N": n,
            "ring": 2 * (n - 1),
            "recursive_doubling": log2n,
            "rabenseifner": 2 * log2n,
        })
    return rows
