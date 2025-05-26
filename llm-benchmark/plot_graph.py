#!/usr/bin/env python3
"""plot_accuracy_latency.py

Create a scatter‑plot of LLM benchmark results.

Usage::

    python plot_graph.py \
        --csvfile total_jcommonsenseqa.csv \
        --server lmstudio \
        --outfile lmstudio_plot.png

Arguments
---------
--csvfile   Path to the benchmark CSV file. Required.
--server    Filter by server name (e.g. "lmstudio" or "modulellm").
            If omitted, all servers are plotted together.
--outfile   Output PNG file path. Required.
--title     Custom title for the plot.

The CSV is expected to have at least these columns (Japanese headers
from your sample):
  * "モデル"          – model name (string)
  * "正解率"          – accuracy like "89%"
  * "平均応答時間"    – latency in milliseconds (numeric string)
  * "サーバー"        – server name (string)

The script converts accuracy to percentage (0‑100) and plots latency on
log scale (ms).  Model names are shown above each marker, rotated 30°.
"""

import argparse
from pathlib import Path
import sys

import matplotlib.pyplot as plt
import pandas as pd


# Windows環境での文字化けを防ぐためにUTF-8に設定
if sys.platform.startswith('win'):
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

# ---------------------------------------------------------------------------
# CLI parsing
# ---------------------------------------------------------------------------

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot accuracy vs latency from a CSV file.")
    parser.add_argument("--csvfile", required=True, type=Path, help="Input CSV file path")
    parser.add_argument("--server", help="Server name to filter (lmstudio, modulellm, ...)")
    parser.add_argument("--outfile", required=True, type=Path, help="Output PNG file path")
    parser.add_argument("--title", help="Custom title for the plot")
    return parser.parse_args(argv)


# ---------------------------------------------------------------------------
# Data processing helpers
# ---------------------------------------------------------------------------

def load_and_prepare(csv_path: Path) -> pd.DataFrame:
    """Load the CSV and return a cleaned DataFrame."""
    df = pd.read_csv(csv_path)

    # Numeric conversions ----------------------------------------------------
    if "正解率" not in df.columns or "平均応答時間" not in df.columns:
        raise ValueError("CSV missing required columns '正解率' or '平均応答時間'.")

    # Accuracy e.g. "89%"  -> 89.0 (percentage)
    df["Accuracy_pct"] = df["正解率"].str.rstrip("%" ).astype(float)
    # Latency assumed ms (string) -> float ms
    df["Latency_ms"]  = pd.to_numeric(df["平均応答時間"], errors="coerce")

    return df


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

def make_plot(df: pd.DataFrame, server: str | None, outfile: Path, title: str | None = None) -> None:
    if server:
        df = df[df["サーバー"] == server]
        if df.empty:
            raise SystemExit(f"No rows found for server '{server}'.")

    fig, ax = plt.subplots(figsize=(9, 6))

    ax.scatter(df["Latency_ms"], df["Accuracy_pct"], zorder=3)

    # Annotate each point -----------------------------------------------------
    for _, row in df.iterrows():
        ax.annotate(
            row["モデル"],
            xy=(row["Latency_ms"], row["Accuracy_pct"]),
            xytext=(0, 6), textcoords="offset points",
            ha="left", va="bottom", rotation=30, fontsize=8, alpha=0.75,
        )

    # Axes formatting ---------------------------------------------------------
    ax.set_xscale("log")
    ax.set_xlabel("Fast <--  Latency (ms)  --> Slow")
    ax.set_ylabel("Poor <--  Accuracy (%)  --> Excellent")
    ax.set_ylim(0, 100)
    
    # タイトルの設定
    if title:
        plot_title = title
    else:
        plot_title = "Accuracy vs Latency" + (f" — Server: {server}" if server else "")
    ax.set_title(plot_title)

    # Grid both directions
    ax.grid(True, which="both", linestyle="--", linewidth=0.5, zorder=0)

    fig.tight_layout()
    fig.savefig(outfile, dpi=150)
    print(f"Saved plot --> {outfile}")


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> None:
    opts = parse_args(argv)

    df = load_and_prepare(opts.csvfile)
    make_plot(df, opts.server, opts.outfile, opts.title)


if __name__ == "__main__":
    main()
