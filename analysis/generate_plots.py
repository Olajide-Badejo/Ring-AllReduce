"""generate_plots.py -- Sec. 6.6's three required figures:

  1. Achieved busbw vs message size (log-x), one line per N, one panel per
     algorithm, with the fitted 1/beta theoretical asymptote as a dashed
     reference.
  2. Efficiency heatmap: N x message size grid, color = achieved/theoretical.
  3. Latency decomposition bar chart at the smallest message size: modeled
     2(N-1)*alpha term vs bandwidth term vs the measured pingpong reference,
     per N.

ACHIEVED_STAT fixes which time_stat represents "achieved" performance
(median, not min -- see docs/DESIGN_DECISIONS.md for why) for every plot in
this module.
"""

import matplotlib

matplotlib.use("Agg")  # no display in this environment; write files directly
import matplotlib.pyplot as plt
import numpy as np

from load_results import algorithms_present, human_bytes, rows_for_stat
from theoretical_model import (
    efficiency,
    fit_alpha_beta,
    theoretical_peak_busbw_GBps,
)

ACHIEVED_STAT = "median"

ALGO_LABELS = {
    "ring": "custom ring",
    "mpi-default": "MPI_Allreduce, default",
    "mpi-ring": "MPI_Allreduce, forced ring",
}
ALGO_COLORS = {"ring": "#1b6ca8", "mpi-default": "#c1440e", "mpi-ring": "#4c9a2a"}


def fit_all_algorithms(df):
    """Returns {algorithm: (alpha, beta, r_squared)}, fit against
    ACHIEVED_STAT rows for each algorithm independently."""
    med = rows_for_stat(df, ACHIEVED_STAT)
    fits = {}
    for algo in algorithms_present(df):
        sub = med[med["algorithm"] == algo]
        fits[algo] = fit_alpha_beta(
            sub["num_procs"], sub["message_size_bytes"], sub["time_seconds"]
        )
    return fits


def plot_busbw_vs_size(df, fits, out_path, n_subset=(2, 4, 8, 16)):
    med = rows_for_stat(df, ACHIEVED_STAT)
    algos = algorithms_present(df)
    fig, axes = plt.subplots(1, len(algos), figsize=(5.2 * len(algos), 4.2), sharey=True)
    axes = np.atleast_1d(axes)

    available_n = sorted(med["num_procs"].unique())
    n_values = [n for n in n_subset if n in available_n] or available_n
    cmap = plt.get_cmap("viridis")

    for ax, algo in zip(axes, algos):
        sub_algo = med[med["algorithm"] == algo]
        for i, n in enumerate(n_values):
            sub_n = sub_algo[sub_algo["num_procs"] == n].sort_values("message_size_bytes")
            if sub_n.empty:
                continue
            color = cmap(i / max(len(n_values) - 1, 1))
            ax.plot(
                sub_n["message_size_bytes"], sub_n["busbw_GBps"],
                marker="o", markersize=3, linewidth=1.5, color=color, label=f"N={n}",
            )
        alpha, beta, _r2 = fits[algo]
        peak = theoretical_peak_busbw_GBps(beta)
        if np.isfinite(peak):
            ax.axhline(
                peak, linestyle="--", color="0.3", linewidth=1.2,
                label=f"theoretical peak, 1/beta = {peak:.3f} GB/s",
            )
        ax.set_xscale("log", base=2)
        ax.set_xlabel("Message size (bytes)")
        ax.set_title(ALGO_LABELS.get(algo, algo))
        ax.grid(True, which="both", linestyle=":", linewidth=0.5, alpha=0.6)
        # Upper left: the curves all start near zero at the small-message end,
        # so that corner is empty. Lower right sits on top of the large-N
        # curve tails and hides them.
        ax.legend(fontsize=7, loc="upper left")

    axes[0].set_ylabel("Bus bandwidth, busbw (GB/s)")
    fig.suptitle("Achieved bus bandwidth vs message size, by process count")
    fig.tight_layout()
    fig.savefig(out_path)
    # Also emit a raster sibling so the README can preview this headline
    # figure without a PDF viewer (docs/assets/busbw_preview.png is copied
    # from here by run_full_analysis.py).
    if out_path.endswith(".pdf"):
        fig.savefig(out_path[:-4] + ".png", dpi=140)
    plt.close(fig)


def plot_efficiency_heatmap(df, fits, out_path):
    med = rows_for_stat(df, ACHIEVED_STAT)
    algos = algorithms_present(df)
    n_values = sorted(med["num_procs"].unique())
    size_values = sorted(med["message_size_bytes"].unique())

    # Compute every panel's grid first so all panels can share one color
    # scale. On single-node shared-memory transport the fitted 1/beta is a
    # sustained-bandwidth average across the whole size range, not a hard
    # ceiling: cache-resident mid-size messages can move bytes faster than
    # that average, so efficiency legitimately exceeds 1 there. Clamping vmax
    # to 1 would hide exactly that effect, so vmax adapts to the data.
    grids = {}
    for algo in algos:
        _alpha, beta, _r2 = fits[algo]
        peak = theoretical_peak_busbw_GBps(beta)
        sub_algo = med[med["algorithm"] == algo]
        grid = np.full((len(n_values), len(size_values)), np.nan)
        for i, n in enumerate(n_values):
            for j, size in enumerate(size_values):
                row = sub_algo[
                    (sub_algo["num_procs"] == n) & (sub_algo["message_size_bytes"] == size)
                ]
                if not row.empty:
                    grid[i, j] = efficiency(float(row["busbw_GBps"].iloc[0]), peak)
        grids[algo] = grid

    finite_maxes = [np.nanmax(g) for g in grids.values() if np.isfinite(np.nanmax(g))]
    vmax = max(1.0, np.ceil(max(finite_maxes) * 10.0) / 10.0) if finite_maxes else 1.0

    fig, axes = plt.subplots(1, len(algos), figsize=(5.6 * len(algos), 4.6))
    axes = np.atleast_1d(axes)
    image = None

    for ax, algo in zip(axes, algos):
        image = ax.imshow(grids[algo], aspect="auto", origin="lower", vmin=0, vmax=vmax, cmap="magma")
        ax.set_xticks(range(len(size_values)))
        ax.set_xticklabels([human_bytes(s) for s in size_values], rotation=90, fontsize=6)
        ax.set_yticks(range(len(n_values)))
        ax.set_yticklabels(n_values, fontsize=7)
        ax.set_xlabel("Message size")
        ax.set_title(ALGO_LABELS.get(algo, algo))

    axes[0].set_ylabel("Process count N")
    fig.suptitle("Efficiency: achieved busbw / theoretical peak busbw (1/beta)")
    fig.tight_layout(rect=(0, 0, 0.92, 1))
    if image is not None:
        cbar_ax = fig.add_axes((0.94, 0.15, 0.015, 0.7))
        fig.colorbar(image, cax=cbar_ax, label="efficiency")
    fig.savefig(out_path)
    plt.close(fig)


def plot_latency_decomposition(df, fits, pingpong_df, out_path):
    smallest_size = float(df["message_size_bytes"].min())
    med = rows_for_stat(df, ACHIEVED_STAT)
    ring_alpha, ring_beta, _r2 = fits.get("ring", (0.0, 0.0, float("nan")))

    n_values = sorted(med[med["algorithm"] == "ring"]["num_procs"].unique())
    if not n_values:
        n_values = sorted(med["num_procs"].unique())

    pp_med = rows_for_stat(pingpong_df, ACHIEVED_STAT)
    pp_med = pp_med.copy()
    pp_med["dist"] = (pp_med["message_size_bytes"] - smallest_size).abs()
    pp_row = pp_med.sort_values("dist").head(1)
    pp_latency_s = float(pp_row["time_seconds"].iloc[0]) if not pp_row.empty else float("nan")

    alpha_terms_us = [2.0 * (n - 1) * ring_alpha * 1e6 for n in n_values]
    beta_terms_us = [
        (2.0 * (n - 1) / n * smallest_size * ring_beta * 1e6) if n > 1 else 0.0 for n in n_values
    ]

    x = np.arange(len(n_values))
    width = 0.35
    fig, ax = plt.subplots(figsize=(7.5, 4.6))
    ax.bar(x - width / 2, alpha_terms_us, width, label="modeled latency term: 2(N-1) alpha",
           color=ALGO_COLORS["ring"])
    ax.bar(x + width / 2, beta_terms_us, width, label="modeled bandwidth term: 2(N-1)/N M beta",
           color=ALGO_COLORS["mpi-default"])
    if np.isfinite(pp_latency_s):
        ax.axhline(
            pp_latency_s * 1e6, linestyle="--", color="0.2", linewidth=1.3,
            label=f"raw pingpong one-way latency = {pp_latency_s * 1e6:.2f} us",
        )
    ax.set_xticks(x)
    ax.set_xticklabels(n_values)
    ax.set_xlabel("Process count N")
    ax.set_ylabel("Time (microseconds)")
    ax.set_title(f"Latency decomposition at the smallest message size ({human_bytes(smallest_size)})")
    ax.legend(fontsize=8)
    ax.grid(True, axis="y", linestyle=":", alpha=0.6)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def generate_all_plots(df, pingpong_df, out_dir):
    """Fits every algorithm once, then produces all three required figures.
    Returns the {algorithm: (alpha, beta, r_squared)} fit dict so callers
    (generate_tables.py, run_full_analysis.py) can reuse the same fits
    rather than recomputing them."""
    fits = fit_all_algorithms(df)
    plot_busbw_vs_size(df, fits, f"{out_dir}/busbw_vs_size.pdf")
    plot_efficiency_heatmap(df, fits, f"{out_dir}/efficiency_heatmap.pdf")
    plot_latency_decomposition(df, fits, pingpong_df, f"{out_dir}/latency_decomposition.pdf")
    return fits
