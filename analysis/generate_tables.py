"""generate_tables.py -- Sec. 6.6's two required tables, written as proper
booktabs LaTeX (\\toprule/\\midrule/\\bottomrule), not a raw
pandas.to_latex() dump:

  1. The Sec. 6.3 step-count table (ring / recursive doubling / Rabenseifner)
     for N = 2..16.
  2. Summary table: achieved busbw at the smallest and largest swept message
     sizes, for every algorithm present, with percent of theoretical peak.
"""

from load_results import algorithms_present, human_bytes, rows_for_stat
from theoretical_model import efficiency, step_count_table, theoretical_peak_busbw_GBps

ACHIEVED_STAT = "median"


def render_step_count_table(n_values, out_path):
    rows = step_count_table(n_values)
    lines = [
        "\\begin{tabular}{rrrr}",
        "\\toprule",
        "$N$ & Ring: $2(N-1)$ & Recursive doubling: $\\lceil\\log_2 N\\rceil$ "
        "& Rabenseifner: $2\\lceil\\log_2 N\\rceil$ \\\\",
        "\\midrule",
    ]
    for row in rows:
        lines.append(
            f"{row['N']} & {row['ring']} & {row['recursive_doubling']} & {row['rabenseifner']} \\\\"
        )
    lines += ["\\bottomrule", "\\end{tabular}"]
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")


def render_summary_table(df, fits, out_path, summary_n=None):
    """summary_n: the process count the summary is reported at (defaults to
    the largest N present in the data -- the most demanding, most
    informative single configuration; averaging across N would blend
    together regimes with very different busbw at the same message size
    and would not describe any one real configuration, see
    docs/DESIGN_DECISIONS.md)."""
    med = rows_for_stat(df, ACHIEVED_STAT)
    algos = algorithms_present(df)
    small = df["message_size_bytes"].min()
    large = df["message_size_bytes"].max()
    if summary_n is None:
        summary_n = int(med["num_procs"].max())

    lines = [
        "\\begin{tabular}{lrrrr}",
        "\\toprule",
        f"Algorithm & busbw @ {human_bytes(small)} (GB/s) & \\% peak "
        f"& busbw @ {human_bytes(large)} (GB/s) & \\% peak \\\\",
        "\\midrule",
    ]
    for algo in algos:
        _alpha, beta, _r2 = fits[algo]
        peak = theoretical_peak_busbw_GBps(beta)
        sub = med[(med["algorithm"] == algo) & (med["num_procs"] == summary_n)]

        row_small = sub[sub["message_size_bytes"] == small]
        row_large = sub[sub["message_size_bytes"] == large]
        busbw_small = float(row_small["busbw_GBps"].iloc[0]) if not row_small.empty else float("nan")
        busbw_large = float(row_large["busbw_GBps"].iloc[0]) if not row_large.empty else float("nan")
        pct_small = efficiency(busbw_small, peak) * 100.0
        pct_large = efficiency(busbw_large, peak) * 100.0

        algo_label = algo.replace("_", "-")
        lines.append(
            f"{algo_label} & {busbw_small:.4f} & {pct_small:.2f} "
            f"& {busbw_large:.3f} & {pct_large:.2f} \\\\"
        )
    lines += ["\\bottomrule", "\\end{tabular}"]
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return summary_n


def generate_all_tables(df, fits, out_dir):
    n_values = list(range(2, 17))
    render_step_count_table(n_values, f"{out_dir}/step_count_table.tex")
    summary_n = render_summary_table(df, fits, f"{out_dir}/summary_table.tex")
    return summary_n
