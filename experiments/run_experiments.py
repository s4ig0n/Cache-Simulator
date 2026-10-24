"""Runs the cache simulator many times, changing one setting at a time, and graphs the results.

Usage (after building cachesim and recording traces with scripts/make_traces.sh):
    python experiments/run_experiments.py

Writes results/<experiment>.csv (the raw numbers) and results/<experiment>.png (the graph).
"""
import csv
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import matplotlib

matplotlib.use("Agg")  # draw straight to files, no window
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FuncFormatter, NullLocator

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, "cachesim.exe" if os.name == "nt" else "cachesim")
TRACES = os.path.join(ROOT, "traces")
RESULTS = os.path.join(ROOT, "results")

WORKLOADS = ["sequential", "random", "matmul", "histogram"]
COLORS = {"sequential": "#2a78d6", "random": "#eb6834", "matmul": "#1baf7a", "histogram": "#eda100"}
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e1e0d9"

plt.rcParams.update({
    "figure.facecolor": "#fcfcfb", "axes.facecolor": "#fcfcfb", "savefig.facecolor": "#fcfcfb",
    "font.size": 10, "axes.titlesize": 11, "axes.titlelocation": "left",
    "axes.spines.top": False, "axes.spines.right": False, "axes.edgecolor": "#c3c2b7",
    "axes.labelcolor": MUTED, "xtick.color": MUTED, "ytick.color": MUTED,
    "axes.grid": True, "axes.grid.axis": "y", "grid.color": GRID, "grid.linewidth": 0.6,
    "axes.axisbelow": True, "lines.linewidth": 2, "lines.markersize": 6,
    "legend.frameon": False, "figure.dpi": 130,
})
PERCENT = FuncFormatter(lambda v, _: f"{v:g}%")


# ------------------------------------------------------------ running the simulator

def simulate(workload, options):
    """Runs cachesim once and pulls the numbers we care about out of its printout."""
    cmd = [SIM] + options + [os.path.join(TRACES, workload + ".trace")]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout

    def number(pattern):
        match = re.search(pattern, out, re.MULTILINE)
        return float(match.group(1)) if match else None

    return {
        "miss_rate": number(r"^miss rate:\s+([\d.]+)%"),        # L1 (its lines have no prefix)
        "l2_miss_rate": number(r"^L2 miss rate:\s+([\d.]+)%"),
        "bytes_read": number(r"^\s+bytes read:\s+(\d+)"),       # to/from main memory
        "bytes_written": number(r"^\s+bytes written:\s+(\d+)"),
        "amat": number(r"^AMAT:\s+([\d.]+)"),
    }


def run_all(runs):
    """runs = list of (label dict, workload, options). Runs them in parallel, returns rows."""
    def one(run):
        label, workload, options = run
        row = {"workload": workload, **label}
        row.update(simulate(workload, options))
        return row

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        return list(pool.map(one, runs))


def save_csv(name, rows):
    with open(os.path.join(RESULTS, name + ".csv"), "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def value(rows, column, **match):
    """The `column` of the one row whose fields equal everything in `match`."""
    found = [r for r in rows if all(r[k] == v for k, v in match.items())]
    assert len(found) == 1, (match, len(found))
    return found[0][column]


def save_plot(fig, name, title, subtitle, legend_from=None):
    """Adds a title, a subtitle and (from legend_from's lines) one legend row above the plots."""
    fig.suptitle(title, x=0.01, ha="left", fontsize=13, fontweight="bold", color=INK)
    fig.text(0.01, 0.905, subtitle, ha="left", color=MUTED)
    top = 0.88
    if legend_from is not None:
        handles, labels = legend_from.get_legend_handles_labels()
        fig.legend(handles, labels, loc="upper left", bbox_to_anchor=(0.005, 0.87), ncol=len(labels))
        top = 0.80
    fig.tight_layout(rect=(0, 0, 1, top))
    fig.savefig(os.path.join(RESULTS, name + ".png"))
    plt.close(fig)
    print(f"  wrote results/{name}.png and results/{name}.csv")


def log_percent(ax):
    ax.set_yscale("log")
    ax.yaxis.set_major_locator(FixedLocator([0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100]))
    ax.yaxis.set_major_formatter(PERCENT)
    ax.yaxis.set_minor_locator(NullLocator())


def categories(ax, labels, xlabel):
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels)
    ax.set_xlabel(xlabel)


# --------------------------------------------------------------------- experiments

ASSOCS = ["1", "2", "4", "8", "16", "full"]


def associativity():
    """How does miss rate change as associativity goes from 1 (direct-mapped) to full?"""
    runs = [({"size": "16K", "assoc": a}, w, ["-s", "16K", "-a", a]) for w in WORKLOADS for a in ASSOCS]
    runs += [({"size": s, "assoc": a}, "matmul", ["-s", s, "-a", a])
             for s in ["8K", "32K"] for a in ASSOCS]
    rows = run_all(runs)
    save_csv("associativity", rows)

    fig, (left, right) = plt.subplots(1, 2, figsize=(11, 4.6))
    for w in WORKLOADS:
        left.plot([value(rows, "miss_rate", workload=w, size="16K", assoc=a) for a in ASSOCS],
                  marker="o", color=COLORS[w], label=w)
    left.set_title("All workloads, 16 KB cache")
    # One hue, light to dark = small to big cache. 8K is drawn wide underneath because
    # its numbers are almost identical to 16K's and would otherwise be hidden.
    for size, shade, width in [("8K", "#86b6ef", 6), ("16K", "#2a78d6", 2), ("32K", "#104281", 2)]:
        right.plot([value(rows, "miss_rate", workload="matmul", size=size, assoc=a) for a in ASSOCS],
                   marker="o", color=shade, linewidth=width, label=f"{size} cache")
    right.set_title("matmul at three cache sizes (8K and 16K overlap)")
    right.yaxis.set_major_formatter(PERCENT)
    right.set_ylim(bottom=0)
    right.legend(loc="center left")
    log_percent(left)
    for ax in (left, right):
        categories(ax, ["1\n(direct)", "2", "4", "8", "16", "full"], "ways per set")
        ax.set_ylabel("miss rate")
    save_plot(fig, "associativity", "Miss rate vs. associativity",
              "64 B blocks, LRU. Left panel: log scale. More ways per set helps some programs a lot "
              "and others not at all.", legend_from=left)


SIZES = ["1K", "2K", "4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K", "1M", "2M", "4M"]


def cache_size():
    """How big does the cache need to be?"""
    rows = run_all([({"size": s}, w, ["-s", s]) for w in WORKLOADS for s in SIZES])
    save_csv("cache_size", rows)

    fig, ax = plt.subplots(figsize=(9, 4.6))
    for w in WORKLOADS:
        ax.plot([value(rows, "miss_rate", workload=w, size=s) for s in SIZES],
                marker="o", color=COLORS[w], label=w)
    categories(ax, SIZES, "cache size")
    log_percent(ax)
    ax.set_ylabel("miss rate")
    save_plot(fig, "cache_size", "Miss rate vs. cache size",
              "8-way, 64 B blocks, LRU. Log scale. Misses drop sharply once a program's data fits.",
              legend_from=ax)


BLOCKS = ["16", "32", "64", "128", "256", "512"]


def block_size():
    """Bigger blocks: fewer misses, but more bytes dragged in per miss?"""
    rows = run_all([({"block": b}, w, ["-b", b]) for w in WORKLOADS for b in BLOCKS])
    save_csv("block_size", rows)

    fig, (left, right) = plt.subplots(1, 2, figsize=(11, 4.6))
    for w in WORKLOADS:
        left.plot([value(rows, "miss_rate", workload=w, block=b) for b in BLOCKS],
                  marker="o", color=COLORS[w], label=w)
        right.plot([value(rows, "bytes_read", workload=w, block=b) / 2**20 for b in BLOCKS],
                   marker="o", color=COLORS[w], label=w)
    left.set_title("Miss rate")
    log_percent(left)
    right.set_title("Data read from memory")
    right.set_yscale("log")
    right.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g} MB"))
    right.yaxis.set_minor_locator(NullLocator())
    for ax in (left, right):
        categories(ax, BLOCKS, "block size (bytes)")
    save_plot(fig, "block_size", "Block size: fewer misses, but more data moved",
              "32 KB, 8-way, LRU. Log scale.", legend_from=left)


def grouped_bars(ax, groups, series, colors, height_of, labels=None):
    """Draws bars for each (group, series) pair side by side."""
    width = 0.8 / len(series)
    for i, s in enumerate(series):
        xs = [g + (i - (len(series) - 1) / 2) * width for g in range(len(groups))]
        ax.bar(xs, [height_of(group, s) for group in groups], width=width,
               color=colors[i], edgecolor="#fcfcfb", label=(labels or series)[i])
    ax.set_xticks(range(len(groups)))
    ax.set_xticklabels(groups)


def replacement():
    """LRU vs FIFO: does it matter who gets kicked out?"""
    rows = run_all([({"size": s, "policy": p}, w, ["-s", s, "-p", p])
                    for w in WORKLOADS for s in ["8K", "32K"] for p in ["lru", "fifo"]])
    save_csv("replacement", rows)

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.4), sharey=True)
    for ax, size in zip(axes, ["8K", "32K"]):
        grouped_bars(ax, WORKLOADS, ["lru", "fifo"], ["#2a78d6", "#eb6834"],
                     lambda w, p: value(rows, "miss_rate", workload=w, size=size, policy=p),
                     labels=["LRU", "FIFO"])
        ax.set_title(f"{size} cache")
        log_percent(ax)
    axes[0].set_ylabel("miss rate")
    axes[0].set_ylim(0.2, 100)
    save_plot(fig, "replacement", "LRU vs. FIFO replacement",
              "8-way, 64 B blocks. Log scale. On these programs the two policies are almost identical.",
              legend_from=axes[0])


def write_policy():
    """Write-back vs write-through: how much do we bother main memory?"""
    rows = run_all([({"write": p}, w, ["-w", p]) for w in WORKLOADS for p in ["wb", "wt"]])
    save_csv("write_policy", rows)

    fig, (left, right) = plt.subplots(1, 2, figsize=(11, 4.4))
    labels = ["write-back + allocate", "write-through + no-allocate"]
    for ax, column, title in [(left, "bytes_written", "Bytes written to memory"),
                              (right, "bytes_read", "Bytes read from memory")]:
        grouped_bars(ax, WORKLOADS, ["wb", "wt"], ["#2a78d6", "#eb6834"],
                     lambda w, p: value(rows, column, workload=w, write=p) / 1024, labels=labels)
        ax.set_title(title)
        ax.set_yscale("log")
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:,.0f} KB"))
        ax.yaxis.set_minor_locator(NullLocator())
    save_plot(fig, "write_policy", "Write-back vs. write-through",
              "32 KB, 8-way, 64 B blocks, LRU. Log scale.", legend_from=left)


L2_SIZES = ["none", "64K", "128K", "256K", "512K", "1M", "2M", "4M", "8M"]


def l2_amat():
    """Does adding an L2 cache make programs faster?"""
    rows = run_all([({"l2": s}, w, [] if s == "none" else ["--l2-size", s])
                    for w in WORKLOADS for s in L2_SIZES])
    save_csv("l2_amat", rows)

    fig, (left, right) = plt.subplots(1, 2, figsize=(11, 4.6))
    for ax, workloads in [(left, WORKLOADS), (right, [w for w in WORKLOADS if w != "random"])]:
        for w in workloads:
            ax.plot([value(rows, "amat", workload=w, l2=s) for s in L2_SIZES],
                    marker="o", color=COLORS[w], label=w)
        categories(ax, ["no L2"] + L2_SIZES[1:], "L2 size")
        ax.set_ylim(bottom=0)
        ax.set_ylabel("cycles per access (AMAT)")
    left.set_title("All workloads")
    right.set_title("Zoomed in, without random")
    save_plot(fig, "l2_amat", "Average memory access time vs. L2 size",
              "L1: 32 KB 8-way, 4 cycles. L2: 8-way, 12 cycles. Memory: 100 cycles. Lower is faster.",
              legend_from=left)


EXPERIMENTS = [associativity, cache_size, block_size, replacement, write_policy, l2_amat]

if __name__ == "__main__":
    if not os.path.exists(SIM):
        sys.exit(f"build the simulator first ({SIM} not found)")
    missing = [w for w in WORKLOADS if not os.path.exists(os.path.join(TRACES, w + ".trace"))]
    if missing:
        sys.exit(f"missing traces for {', '.join(missing)}: run ./scripts/make_traces.sh in WSL/Linux")
    os.makedirs(RESULTS, exist_ok=True)
    for experiment in EXPERIMENTS:
        print(f"{experiment.__name__}: {experiment.__doc__}")
        experiment()
