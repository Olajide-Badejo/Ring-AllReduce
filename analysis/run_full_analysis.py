#!/usr/bin/env python3
"""run_full_analysis.py -- single entry point for the analysis pipeline.

Reads a results CSV and a pingpong CSV, writes every Sec. 6.6 figure to
<outdir>/figures/*.pdf and every required table to <outdir>/tables/*.tex.
Idempotent: rerunning always overwrites its own output files in place, so
it is always safe to rerun.

Usage:
  python3 analysis/run_full_analysis.py
  python3 analysis/run_full_analysis.py --results path/to/results.csv \\
      --pingpong path/to/pingpong.csv --outdir report

With no arguments, analyzes the committed results/sample_run/ dataset (see
that directory's README.md before citing anything from that default run --
it is synthetic, not a real hardware measurement) and writes into report/.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from generate_plots import generate_all_plots  # noqa: E402
from generate_tables import generate_all_tables  # noqa: E402
from load_results import load_pingpong_csv, load_results_csv  # noqa: E402


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description="Run the ring-allreduce analysis pipeline")
    parser.add_argument(
        "--results", default=os.path.join(repo_root, "results", "sample_run", "results.csv"),
        help="path to a benchmark results CSV (apps/benchmark.cpp's schema)",
    )
    parser.add_argument(
        "--pingpong", default=os.path.join(repo_root, "results", "sample_run", "pingpong.csv"),
        help="path to a pingpong CSV (apps/pingpong.cpp's schema)",
    )
    parser.add_argument(
        "--outdir", default=os.path.join(repo_root, "report"),
        help="writes <outdir>/figures/*.pdf and <outdir>/tables/*.tex",
    )
    args = parser.parse_args()

    figures_dir = os.path.join(args.outdir, "figures")
    tables_dir = os.path.join(args.outdir, "tables")
    os.makedirs(figures_dir, exist_ok=True)
    os.makedirs(tables_dir, exist_ok=True)

    print(f"loading results: {args.results}")
    df = load_results_csv(args.results)
    print(f"loading pingpong: {args.pingpong}")
    pingpong_df = load_pingpong_csv(args.pingpong)
    print(f"{len(df)} result rows, {len(pingpong_df)} pingpong rows")

    print("fitting alpha/beta per algorithm and generating figures...")
    fits = generate_all_plots(df, pingpong_df, figures_dir)
    for algo, (alpha, beta, r_squared) in fits.items():
        peak_GBps = (1.0 / beta / 1e9) if beta > 0 else float("inf")
        print(
            f"  {algo}: alpha={alpha * 1e6:.3f} us, beta={beta * 1e9:.4f} ns/byte, "
            f"1/beta={peak_GBps:.3f} GB/s, R^2={r_squared:.4f}"
        )

    print("generating tables...")
    summary_n = generate_all_tables(df, fits, tables_dir)
    print(f"  summary table reported at N={summary_n}")

    print(f"done. figures: {figures_dir}/  tables: {tables_dir}/")


if __name__ == "__main__":
    main()
