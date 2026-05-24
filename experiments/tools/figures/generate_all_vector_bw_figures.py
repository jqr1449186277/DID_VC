from __future__ import annotations

from pathlib import Path
import argparse
import re
import zipfile

import matplotlib.pyplot as plt
import pandas as pd


# --------------------------
# Global plotting defaults
# --------------------------
plt.rcParams["svg.fonttype"] = "none"
plt.rcParams["pdf.fonttype"] = 42
plt.rcParams["font.size"] = 11

VECTOR_EXTS = ("svg", "pdf")
BAR_FACE = "white"
BAR_EDGE = "black"
GRID_COLOR = "0.7"
GRID_STYLE = ":"
GRID_WIDTH = 0.7

BASELINE_HATCH_A = "///"
BASELINE_HATCH_B = "xxx"
EXP34_HATCHES = ["///", "\\\\", "xxx"]

DEPTH_STYLES = [
    {"label": "path", "linestyle": "-", "marker": "o"},
    {"label": "witness", "linestyle": "--", "marker": "s"},
    {"label": "prove", "linestyle": "-.", "marker": "^"},
    {"label": "verify", "linestyle": ":", "marker": "D"},
]
NT_STYLES = [
    {"label": "recover", "linestyle": "-", "marker": "o"},
    {"label": "trace-run", "linestyle": "--", "marker": "s"},
    {"label": "trace-verify", "linestyle": "-.", "marker": "^"},
    {"label": "publish", "linestyle": ":", "marker": "D"},
]


def style_axis(ax, spine_width: float = 1.0, tick_width: float = 1.0) -> None:
    ax.set_facecolor("white")
    for spine in ax.spines.values():
        spine.set_linewidth(spine_width)
    ax.tick_params(width=tick_width)
    ax.grid(axis="y", linestyle=GRID_STYLE, linewidth=GRID_WIDTH, color=GRID_COLOR)
    ax.set_axisbelow(True)


def save_vector(fig, base_path: Path) -> list[Path]:
    fig.tight_layout()
    written = []
    for ext in VECTOR_EXTS:
        out = base_path.with_suffix(f".{ext}")
        fig.savefig(out, bbox_inches="tight")
        written.append(out)
    plt.close(fig)
    return written


# --------------------------
# Baseline section figures
# --------------------------
BASELINE_AUTH_BB1 = "baseline_100/20260328_141001_G_zk_auth/zk_auth_bb1.csv"
BASELINE_RECOVERY = "baseline_100/20260328_145129_H_zk_recovery_batch/zk_recovery_batch.csv"
BASELINE_METRICS = ["path_fetch_ms", "witness_ms", "prove_ms", "verify_ms"]
BASELINE_LABELS = ["path", "witness", "prove", "verify"]


def draw_pcr_bars(ax, old_invalid: float, new_valid: float) -> None:
    labels = ["old proof invalid", "new proof valid"]
    vals = [old_invalid, new_valid]
    bars = ax.bar(
        labels,
        vals,
        color=[BAR_FACE, BAR_FACE],
        edgecolor=BAR_EDGE,
        linewidth=1.2,
        hatch=[BASELINE_HATCH_A, BASELINE_HATCH_B],
    )
    ax.set_ylabel("Rate (%)")
    ax.set_ylim(0, 110)
    ax.set_title("PCR outcome rates (100 runs)")
    style_axis(ax)
    for bar, val in zip(bars, vals):
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            val + 2,
            f"{val:.0f}%",
            ha="center",
            va="bottom",
            fontsize=10,
        )


def draw_baseline_cost_bars(ax, baseline_vals: list[float], recovery_vals: list[float]) -> None:
    x = list(range(len(BASELINE_METRICS)))
    width = 0.38
    ax.bar(
        [i - width / 2 for i in x],
        baseline_vals,
        width=width,
        label="baseline auth",
        color=BAR_FACE,
        edgecolor=BAR_EDGE,
        linewidth=1.2,
        hatch=BASELINE_HATCH_A,
    )
    ax.bar(
        [i + width / 2 for i in x],
        recovery_vals,
        width=width,
        label="post-recovery auth",
        color=BAR_FACE,
        edgecolor=BAR_EDGE,
        linewidth=1.2,
        hatch=BASELINE_HATCH_B,
    )
    ax.set_xticks(x)
    ax.set_xticklabels(BASELINE_LABELS)
    ax.set_ylabel("Median time (ms)")
    ax.set_title("Baseline vs post-recovery cost (100 runs)")
    ax.legend(frameon=True, edgecolor="black")
    style_axis(ax)


def run_baseline(baseline_zip: Path, out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(baseline_zip) as zf:
        auth = pd.read_csv(zf.open(BASELINE_AUTH_BB1))
        rec = pd.read_csv(zf.open(BASELINE_RECOVERY))

    baseline_vals = [float(auth[m].median()) for m in BASELINE_METRICS]
    recovery_vals = [float(rec[m].median()) for m in BASELINE_METRICS]
    old_invalid = float((rec["old_proof_valid"] == 0).mean() * 100.0)
    new_valid = float((rec["new_proof_valid"] == 1).mean() * 100.0)

    written: list[Path] = []

    fig, ax = plt.subplots(figsize=(5.2, 4.2))
    draw_pcr_bars(ax, old_invalid, new_valid)
    written.extend(save_vector(fig, out_dir / "pcr_outcome_rates_100runs_bw"))

    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    draw_baseline_cost_bars(ax, baseline_vals, recovery_vals)
    written.extend(save_vector(fig, out_dir / "baseline_vs_recovery_cost_100runs_bw"))

    fig, axes = plt.subplots(1, 2, figsize=(12.0, 4.4), constrained_layout=True)
    draw_pcr_bars(axes[0], old_invalid, new_valid)
    draw_baseline_cost_bars(axes[1], baseline_vals, recovery_vals)
    fig.suptitle("Baseline experiment figure")
    for ext in VECTOR_EXTS:
        out = (out_dir / "baseline_experiment_combined_100runs_bw").with_suffix(f".{ext}")
        fig.savefig(out, bbox_inches="tight")
        written.append(out)
    plt.close(fig)

    summary = pd.DataFrame([
        {"series": "baseline_auth", "metric": "path", "median_ms": baseline_vals[0]},
        {"series": "baseline_auth", "metric": "witness", "median_ms": baseline_vals[1]},
        {"series": "baseline_auth", "metric": "prove", "median_ms": baseline_vals[2]},
        {"series": "baseline_auth", "metric": "verify", "median_ms": baseline_vals[3]},
        {"series": "post_recovery_auth", "metric": "path", "median_ms": recovery_vals[0]},
        {"series": "post_recovery_auth", "metric": "witness", "median_ms": recovery_vals[1]},
        {"series": "post_recovery_auth", "metric": "prove", "median_ms": recovery_vals[2]},
        {"series": "post_recovery_auth", "metric": "verify", "median_ms": recovery_vals[3]},
        {"series": "pcr", "metric": "old_proof_invalid_rate", "median_ms": old_invalid},
        {"series": "pcr", "metric": "new_proof_valid_rate", "median_ms": new_valid},
    ])
    summary_path = out_dir / "baseline_section_summary.csv"
    summary.to_csv(summary_path, index=False)
    written.append(summary_path)
    return written


# --------------------------
# Exp.3 / Exp.4 figures
# --------------------------
EXP34_ATTEMPTS_PATH = "exp_34/20260316_165311_J_ttss_threshold_boundary/attempts.csv"
EXP34_SUMMARY_PATH = "exp_34/20260316_183026_K_ttss_trace_cases/summary.csv"


def build_exp3(attempts: pd.DataFrame) -> pd.DataFrame:
    return (
        attempts.groupby("requested_share_count")
        .agg(
            recover_success_rate=("recover_ok", "mean"),
            avg_recover_ms=("recover_ms", "mean"),
            avg_share_count_used=("share_count_used", "mean"),
        )
        .reset_index()
        .sort_values("requested_share_count")
        .reset_index(drop=True)
    )


def build_exp4(summary: pd.DataFrame) -> pd.DataFrame:
    df = summary.copy()
    df["case_label"] = df.apply(
        lambda r: "noleak"
        if r["case_mode"] == "noleak"
        else ("badmeta" if r["case_mode"] == "badmeta" else f"leak {r['expected_leaked_set']}"),
        axis=1,
    )
    return df.sort_values("case_no").reset_index(drop=True)


def style_exp_axis(ax):
    ax.set_facecolor("white")
    ax.tick_params(labelsize=18, length=8, width=1.2)
    for spine in ax.spines.values():
        spine.set_linewidth(1.5)
    ax.grid(axis="y", linestyle=GRID_STYLE, linewidth=GRID_WIDTH, color=GRID_COLOR)
    ax.set_axisbelow(True)


def draw_exp3(ax1, df: pd.DataFrame):
    x_vals = df["requested_share_count"].tolist()
    x_labels = [str(v) for v in x_vals]
    success = (df["recover_success_rate"] * 100.0).tolist()
    recover = df["avg_recover_ms"].tolist()
    used = df["avg_share_count_used"].tolist()

    bars = ax1.bar(
        x_labels,
        success,
        color=BAR_FACE,
        edgecolor=BAR_EDGE,
        linewidth=1.2,
        hatch=EXP34_HATCHES[0],
        label="success rate",
    )
    ax1.set_ylabel("Recovery success rate (%)")
    ax1.set_xlabel("Requested share count")
    ax1.set_ylim(0, 110)
    ax1.set_title("Exp.3 Threshold boundary", fontsize=24, pad=14)

    ax2 = ax1.twinx()
    ax2.plot(
        x_labels,
        recover,
        color="black",
        linestyle="--",
        marker="s",
        linewidth=2.0,
        markersize=8,
        label="avg recover time",
    )
    ax2.set_ylabel("Avg recover time (ms)")
    ax2.set_ylim(21.4, 24.55)

    for bar, pct in zip(bars, success):
        ax1.text(
            bar.get_x() + bar.get_width() / 2,
            max(1, pct) + (1 if pct > 0 else 0),
            f"{int(round(pct))}%",
            ha="center",
            va="bottom",
            fontsize=20,
        )

    for xi, val in zip(x_labels, recover):
        ax2.text(xi, val + 0.05, f"{val:.1f}", ha="center", va="bottom", fontsize=20)

    for label, u in zip(x_labels, used):
        if u > 0:
            ax1.text(label, 7, f"used={int(round(u))}", ha="center", va="bottom", fontsize=18)

    style_exp_axis(ax1)
    style_exp_axis(ax2)
    handles1, labels1 = ax1.get_legend_handles_labels()
    handles2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(handles1 + handles2, labels1 + labels2, fontsize=18, loc="upper left", frameon=True, edgecolor="black")


def draw_exp4(ax, df: pd.DataFrame):
    labels = df["case_label"].tolist()
    trace = df["avg_trace_run_ms"].tolist()
    verify = df["avg_trace_verify_ms"].tolist()
    publish = df["avg_publish_ms"].tolist()

    x = list(range(len(labels)))
    width = 0.26
    ax.bar([i - width for i in x], trace, width=width, label="trace", color=BAR_FACE, edgecolor=BAR_EDGE, linewidth=1.2, hatch=EXP34_HATCHES[0])
    ax.bar(x, verify, width=width, label="verify", color=BAR_FACE, edgecolor=BAR_EDGE, linewidth=1.2, hatch=EXP34_HATCHES[1])
    ax.bar([i + width for i in x], publish, width=width, label="publish", color=BAR_FACE, edgecolor=BAR_EDGE, linewidth=1.2, hatch=EXP34_HATCHES[2])
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=15)
    ax.set_ylabel("Average time (ms)")
    ax.set_title("Exp.4 Trace/Verify/Publish across cases", fontsize=24, pad=14)
    ax.legend(fontsize=20, loc="upper left", frameon=True, edgecolor="black")
    style_exp_axis(ax)


def run_exp34(exp34_zip: Path, out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(exp34_zip) as zf:
        attempts = pd.read_csv(zf.open(EXP34_ATTEMPTS_PATH))
        summary = pd.read_csv(zf.open(EXP34_SUMMARY_PATH))

    exp3 = build_exp3(attempts)
    exp4 = build_exp4(summary)

    written: list[Path] = []
    exp3_csv = out_dir / "exp3_threshold_summary.csv"
    exp4_csv = out_dir / "exp4_trace_summary.csv"
    exp3.to_csv(exp3_csv, index=False)
    exp4.to_csv(exp4_csv, index=False)
    written.extend([exp3_csv, exp4_csv])

    fig, ax = plt.subplots(figsize=(12.8, 7.2))
    fig.patch.set_facecolor("white")
    draw_exp3(ax, exp3)
    written.extend(save_vector(fig, out_dir / "exp3_threshold_boundary_exact_bw"))

    fig, ax = plt.subplots(figsize=(13.79, 7.68))
    fig.patch.set_facecolor("white")
    draw_exp4(ax, exp4)
    written.extend(save_vector(fig, out_dir / "exp4_trace_cases_exact_bw"))

    fig, (ax_left, ax_right) = plt.subplots(1, 2, figsize=(26.6, 7.8))
    fig.patch.set_facecolor("white")
    draw_exp3(ax_left, exp3)
    draw_exp4(ax_right, exp4)
    fig.tight_layout(w_pad=3.0)
    for ext in VECTOR_EXTS:
        out = (out_dir / "exp34_combined_horizontal_exact_bw").with_suffix(f".{ext}")
        fig.savefig(out, bbox_inches="tight")
        written.append(out)
    plt.close(fig)

    return written


# --------------------------
# Tree depth and (n,t) figures
# --------------------------
TREE_RESULTS_CSV = "TreeDeepth_16_20/run_O_activate_depths_and_test_20260328_205900/results.csv"
TREE_STDOUT_TMPL = "TreeDeepth_16_20/run_O_activate_depths_and_test_20260328_205900/depth_{depth}/auth.stdout.log"
TREE_AUTH_CSV_TMPL = "TreeDeepth_16_20/16_20/{out_tag}_G_zk_auth/zk_auth_bb1.csv"
NT_AGGREGATE_CSV = "ttss_nt/20260328_165935_M_ttss_nt_matrix/aggregate.csv"


def extract_depth_summary(tree_zip: Path) -> pd.DataFrame:
    with zipfile.ZipFile(tree_zip) as zf:
        results = pd.read_csv(zf.open(TREE_RESULTS_CSV))
        rows = []
        for depth in results["depth"].tolist():
            txt = zf.read(TREE_STDOUT_TMPL.format(depth=depth)).decode("utf-8", errors="ignore")
            m = re.search(r"OUT=/home/spike/did-e2e/results/([0-9_]+)_G_zk_auth", txt)
            if not m:
                raise RuntimeError(f"Cannot locate auth output dir for depth {depth}")
            out_tag = m.group(1)
            df = pd.read_csv(zf.open(TREE_AUTH_CSV_TMPL.format(out_tag=out_tag)))
            rows.append({
                "depth": int(depth),
                "runs": int(len(df)),
                "success_rate": float(df["ok"].mean()),
                "path_median_ms": float(df["path_fetch_ms"].median()),
                "witness_median_ms": float(df["witness_ms"].median()),
                "prove_median_ms": float(df["prove_ms"].median()),
                "verify_median_ms": float(df["verify_ms"].median()),
                "proof_bytes_median": float(df["proof_bytes"].median()),
                "public_bytes_median": float(df["public_bytes"].median()),
            })
        return pd.DataFrame(rows).sort_values("depth").reset_index(drop=True)


def extract_nt_summary(nt_zip: Path):
    with zipfile.ZipFile(nt_zip) as zf:
        agg = pd.read_csv(zf.open(NT_AGGREGATE_CSV))
    fixed_t4 = agg[agg["group_name"] == "fixed_t4"].copy().sort_values("n").reset_index(drop=True)
    fixed_n6 = agg[agg["group_name"] == "fixed_n6"].copy().sort_values("t").reset_index(drop=True)
    selected = agg[agg["group_name"] == "selected_points"].copy().sort_values(["n", "t"]).reset_index(drop=True)
    return fixed_t4, fixed_n6, selected


def plot_series(ax, x, series_list, styles, xlabel, ylabel, title):
    for style, y in zip(styles, series_list):
        ax.plot(
            x,
            y,
            color="black",
            linewidth=1.8,
            linestyle=style["linestyle"],
            marker=style["marker"],
            markersize=6,
            label=style["label"],
        )
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend(frameon=True, edgecolor="black")
    style_axis(ax)


def run_depth_nt(tree_zip: Path, nt_zip: Path, out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    depth_df = extract_depth_summary(tree_zip)
    fixed_t4, fixed_n6, selected = extract_nt_summary(nt_zip)

    written: list[Path] = []
    for name, df in [
        ("tree_depth_summary.csv", depth_df),
        ("nt_fixed_t4_summary.csv", fixed_t4),
        ("nt_fixed_n6_summary.csv", fixed_n6),
        ("nt_selected_points_summary.csv", selected),
    ]:
        p = out_dir / name
        df.to_csv(p, index=False)
        written.append(p)

    fig, ax = plt.subplots(figsize=(7.2, 4.8))
    plot_series(
        ax,
        depth_df["depth"],
        [
            depth_df["path_median_ms"],
            depth_df["witness_median_ms"],
            depth_df["prove_median_ms"],
            depth_df["verify_median_ms"],
        ],
        DEPTH_STYLES,
        "Tree depth",
        "Median time (ms)",
        "Anonymous auth cost under different tree depths (bb_each=1)",
    )
    ax.set_xticks(depth_df["depth"].tolist())
    written.extend(save_vector(fig, out_dir / "tree_depth_cost_bw"))

    fig, ax = plt.subplots(figsize=(6.4, 4.8))
    plot_series(
        ax,
        fixed_t4["n"],
        [
            fixed_t4["avg_recover_ms"],
            fixed_t4["avg_trace_run_ms"],
            fixed_t4["avg_trace_verify_ms"],
            fixed_t4["avg_publish_ms"],
        ],
        NT_STYLES,
        "n (t=4)",
        "Average time (ms)",
        "Recovery and tracing cost under fixed t=4",
    )
    ax.set_xticks(fixed_t4["n"].tolist())
    written.extend(save_vector(fig, out_dir / "nt_cost_fixed_t4_bw"))

    fig, ax = plt.subplots(figsize=(6.4, 4.8))
    plot_series(
        ax,
        fixed_n6["t"],
        [
            fixed_n6["avg_recover_ms"],
            fixed_n6["avg_trace_run_ms"],
            fixed_n6["avg_trace_verify_ms"],
            fixed_n6["avg_publish_ms"],
        ],
        NT_STYLES,
        "t (n=6)",
        "Average time (ms)",
        "Recovery and tracing cost under fixed n=6",
    )
    ax.set_xticks(fixed_n6["t"].tolist())
    written.extend(save_vector(fig, out_dir / "nt_cost_fixed_n6_bw"))

    return written


# --------------------------
# CLI entry
# --------------------------
def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate all black-and-white readable vector figures in one run."
    )
    parser.add_argument("--baseline-zip", default="baseline_100.zip", type=Path)
    parser.add_argument("--exp34-zip", default="exp_34.zip", type=Path)
    parser.add_argument("--tree-zip", default="TreeDeepth_16_20.zip", type=Path)
    parser.add_argument("--nt-zip", default="ttss_nt.zip", type=Path)
    parser.add_argument("--baseline-out", default="baseline_section_output", type=Path)
    parser.add_argument("--exp34-out", default="exp34_horizontal_output", type=Path)
    parser.add_argument("--depthnt-out", default="depth_nt_analysis", type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    missing = [
        p for p in [args.baseline_zip, args.exp34_zip, args.tree_zip, args.nt_zip]
        if not p.exists()
    ]
    if missing:
        missing_text = "\n".join(f"- {p}" for p in missing)
        raise SystemExit(f"Missing required input zip file(s):\n{missing_text}")

    written: list[Path] = []
    written.extend(run_baseline(args.baseline_zip, args.baseline_out))
    written.extend(run_exp34(args.exp34_zip, args.exp34_out))
    written.extend(run_depth_nt(args.tree_zip, args.nt_zip, args.depthnt_out))

    print("Wrote:")
    for path in written:
        print(path)


if __name__ == "__main__":
    main()
