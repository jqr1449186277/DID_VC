from pathlib import Path
import zipfile
import pandas as pd
import matplotlib.pyplot as plt

ZIP_PATH = Path("exp_34.zip")
OUT_DIR = Path("exp34_horizontal_output")
OUT_DIR.mkdir(exist_ok=True)

ATTEMPTS_PATH = "exp_34/20260316_165311_J_ttss_threshold_boundary/attempts.csv"
SUMMARY_PATH = "exp_34/20260316_183026_K_ttss_trace_cases/summary.csv"


def load_data():
    with zipfile.ZipFile(ZIP_PATH) as zf:
        attempts = pd.read_csv(zf.open(ATTEMPTS_PATH))
        summary = pd.read_csv(zf.open(SUMMARY_PATH))
    return attempts, summary


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


def style_primary_axis(ax):
    ax.tick_params(labelsize=18, length=8, width=1.2)
    for spine in ax.spines.values():
        spine.set_linewidth(1.5)


def style_secondary_axis(ax):
    ax.tick_params(labelsize=18, length=8, width=1.2)
    for spine in ax.spines.values():
        spine.set_linewidth(1.5)


def draw_exp3(ax1, df: pd.DataFrame):
    x_vals = df["requested_share_count"].tolist()
    x_labels = [str(v) for v in x_vals]
    success = (df["recover_success_rate"] * 100.0).tolist()
    recover = df["avg_recover_ms"].tolist()
    used = df["avg_share_count_used"].tolist()

    ax1.set_facecolor("#eeeeee")
    bars = ax1.bar(x_labels, success)
    ax1.set_ylabel("Recovery success rate (%)")
    ax1.set_xlabel("Requested share count")
    ax1.set_ylim(0, 110)
    ax1.set_title("Exp.3 Threshold boundary", fontsize=24, pad=14)

    ax2 = ax1.twinx()
    ax2.plot(x_labels, recover, marker="o", linewidth=3, markersize=9)
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

    style_primary_axis(ax1)
    style_secondary_axis(ax2)
    return ax2



def draw_exp4(ax, df: pd.DataFrame):
    labels = df["case_label"].tolist()
    trace = df["avg_trace_run_ms"].tolist()
    verify = df["avg_trace_verify_ms"].tolist()
    publish = df["avg_publish_ms"].tolist()

    x = list(range(len(labels)))
    width = 0.26

    ax.set_facecolor("#eeeeee")
    ax.bar([i - width for i in x], trace, width=width, label="trace")
    ax.bar(x, verify, width=width, label="verify")
    ax.bar([i + width for i in x], publish, width=width, label="publish")

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=15)
    ax.set_ylabel("Average time (ms)")
    ax.set_title("Exp.4 Trace/Verify/Publish across cases", fontsize=24, pad=14)
    ax.legend(fontsize=22, loc="upper left")
    style_primary_axis(ax)



def save_figure_in_vector_formats(fig, base_path: Path):
    svg_path = base_path.with_suffix(".svg")
    pdf_path = base_path.with_suffix(".pdf")
    fig.savefig(svg_path, bbox_inches="tight")
    fig.savefig(pdf_path, bbox_inches="tight")
    return svg_path, pdf_path



def plot_exp3(df: pd.DataFrame, base_path: Path):
    fig, ax1 = plt.subplots(figsize=(12.8, 7.2))
    fig.patch.set_facecolor("#eeeeee")
    draw_exp3(ax1, df)
    fig.tight_layout()
    paths = save_figure_in_vector_formats(fig, base_path)
    plt.close(fig)
    return paths



def plot_exp4(df: pd.DataFrame, base_path: Path):
    fig, ax = plt.subplots(figsize=(13.79, 7.68))
    fig.patch.set_facecolor("#eeeeee")
    draw_exp4(ax, df)
    fig.tight_layout()
    paths = save_figure_in_vector_formats(fig, base_path)
    plt.close(fig)
    return paths



def plot_combined_horizontal(exp3_df: pd.DataFrame, exp4_df: pd.DataFrame, base_path: Path):
    fig, (ax_left, ax_right) = plt.subplots(1, 2, figsize=(26.6, 7.8))
    fig.patch.set_facecolor("white")
    draw_exp3(ax_left, exp3_df)
    draw_exp4(ax_right, exp4_df)
    fig.tight_layout(w_pad=3.0)
    paths = save_figure_in_vector_formats(fig, base_path)
    plt.close(fig)
    return paths



def main():
    attempts, summary = load_data()
    exp3 = build_exp3(attempts)
    exp4 = build_exp4(summary)

    exp3.to_csv(OUT_DIR / "exp3_threshold_summary.csv", index=False)
    exp4.to_csv(OUT_DIR / "exp4_trace_summary.csv", index=False)

    exp3_base = OUT_DIR / "exp3_threshold_boundary_exact"
    exp4_base = OUT_DIR / "exp4_trace_cases_exact"
    combo_base = OUT_DIR / "exp34_combined_horizontal_exact"

    exp3_paths = plot_exp3(exp3, exp3_base)
    exp4_paths = plot_exp4(exp4, exp4_base)
    combo_paths = plot_combined_horizontal(exp3, exp4, combo_base)

    print("Wrote:")
    for path in [*exp3_paths, *exp4_paths, *combo_paths, OUT_DIR / "exp3_threshold_summary.csv", OUT_DIR / "exp4_trace_summary.csv"]:
        print(path)


if __name__ == "__main__":
    main()
