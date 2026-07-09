"""load_results.py -- loads and validates the benchmark/pingpong CSVs.

Kept deliberately separate from generate_plots.py / generate_tables.py so
both can share one validated, consistent view of the data (Sec. 1's
directory layout lists this as its own module).
"""

import pandas as pd

REQUIRED_RESULTS_COLUMNS = [
    "algorithm",
    "num_procs",
    "dtype",
    "message_size_bytes",
    "time_stat",
    "time_seconds",
    "algbw_GBps",
    "busbw_GBps",
    "num_reps",
]

REQUIRED_PINGPONG_COLUMNS = [
    "message_size_bytes",
    "time_stat",
    "time_seconds",
    "algbw_GBps",
    "num_reps",
]

VALID_TIME_STATS = {"min", "mean", "median", "max", "stddev"}


def load_results_csv(path):
    """Loads and validates a benchmark results CSV (apps/benchmark.cpp's
    schema). Raises ValueError with a specific message on any structural
    problem, rather than letting a malformed CSV surface as a confusing
    KeyError deep inside plotting code."""
    df = pd.read_csv(path)
    missing = [c for c in REQUIRED_RESULTS_COLUMNS if c not in df.columns]
    if missing:
        raise ValueError(f"{path}: missing required column(s) {missing}")
    bad_stats = set(df["time_stat"].unique()) - VALID_TIME_STATS
    if bad_stats:
        raise ValueError(f"{path}: unexpected time_stat value(s) {bad_stats}")
    if df.empty:
        raise ValueError(f"{path}: no rows found")
    return df


def load_pingpong_csv(path):
    """Loads and validates a pingpong CSV (apps/pingpong.cpp's schema)."""
    df = pd.read_csv(path)
    missing = [c for c in REQUIRED_PINGPONG_COLUMNS if c not in df.columns]
    if missing:
        raise ValueError(f"{path}: missing required column(s) {missing}")
    if df.empty:
        raise ValueError(f"{path}: no rows found")
    return df


def rows_for_stat(df, stat):
    """Rows for a single time_stat (e.g. 'median'), all other columns intact."""
    if stat not in VALID_TIME_STATS:
        raise ValueError(f"rows_for_stat: '{stat}' is not one of {sorted(VALID_TIME_STATS)}")
    return df[df["time_stat"] == stat].copy()


def algorithms_present(df):
    """Algorithm names in the CSV, in a fixed, report-friendly order (any
    algorithm the CSV doesn't happen to contain is simply omitted, so a
    partial/--quick sweep still analyzes cleanly)."""
    preferred_order = ["ring", "mpi-default", "mpi-ring"]
    present = set(df["algorithm"].unique())
    ordered = [a for a in preferred_order if a in present]
    ordered += sorted(present - set(ordered))
    return ordered


def human_bytes(n):
    """Formats a byte count as e.g. '8B', '4KiB', '128MiB' for axis labels
    and table cells (binary/1024-based, matching the 2^3..2^27 sweep)."""
    n = int(n)
    if n < 1024:
        return f"{n}B"
    if n < 1024 ** 2:
        return f"{n / 1024:.0f}KiB"
    if n < 1024 ** 3:
        return f"{n / 1024 ** 2:.0f}MiB"
    return f"{n / 1024 ** 3:.0f}GiB"
