from pathlib import Path
import zipfile
import re
import pandas as pd
import matplotlib.pyplot as plt

TREE_ZIP = Path("TreeDeepth_16_20.zip")
NT_ZIP = Path("ttss_nt.zip")
OUT_DIR = Path("depth_nt_analysis")
OUT_DIR.mkdir(exist_ok=True)


def extract_depth_summary(tree_zip: Path) -> pd.DataFrame:
    with zipfile.ZipFile(tree_zip) as zf:
        results = pd.read_csv(
            zf.open("TreeDeepth_16_20/run_O_activate_depths_and_test_20260328_205900/results.csv")
        )
        rows = []
        for depth in results["depth"].tolist():
            stdout_name = (
                f"TreeDeepth_16_20/run_O_activate_depths_and_test_20260328_205900/"
                f"depth_{depth}/auth.stdout.log"
            )
            txt = zf.read(stdout_name).decode("utf-8", errors="ignore")
            m = re.search(r"OUT=/home/spike/did-e2e/results/([0-9_]+)_G_zk_auth", txt)
            if not m:
                raise RuntimeError(f"Cannot locate auth output dir for depth {depth}")
            out_tag = m.group(1)
            csv_name = f"TreeDeepth_16_20/16_20/{out_tag}_G_zk_auth/zk_auth_bb1.csv"
            df = pd.read_csv(zf.open(csv_name))
            rows.append(
                {
                    "depth": int(depth),
                    "runs": int(len(df)),
                    "success_rate": float(df["ok"].mean()),
                    "path_median_ms": float(df["path_fetch_ms"].median()),
                    "witness_median_ms": float(df["witness_ms"].median()),
                    "prove_median_ms": float(df["prove_ms"].median()),
                    "verify_median_ms": float(df["verify_ms"].median()),
                    "proof_bytes_median": float(df["proof_bytes"].median()),
                    "public_bytes_median": float(df["public_bytes"].median()),
                }
            )
        return pd.DataFrame(rows).sort_values("depth").reset_index(drop=True)



def extract_nt_summary(nt_zip: Path):
    with zipfile.ZipFile(nt_zip) as zf:
        agg = pd.read_csv(zf.open("ttss_nt/20260328_165935_M_ttss_nt_matrix/aggregate.csv"))
    fixed_t4 = agg[agg["group_name"] == "fixed_t4"].copy().sort_values("n").reset_index(drop=True)
    fixed_n6 = agg[agg["group_name"] == "fixed_n6"].copy().sort_values("t").reset_index(drop=True)
    selected = agg[agg["group_name"] == "selected_points"].copy().sort_values(["n", "t"]).reset_index(drop=True)
    return fixed_t4, fixed_n6, selected



def save_figure(fig, stem: Path):
    fig.tight_layout()
    fig.savefig(stem.with_suffix(".svg"), bbox_inches="tight")
    fig.savefig(stem.with_suffix(".pdf"), bbox_inches="tight")
    plt.close(fig)



def plot_tree_depth(depth_df: pd.DataFrame, out_stem: Path):
    fig, ax = plt.subplots(figsize=(7.2, 4.8))
    ax.plot(depth_df["depth"], depth_df["path_median_ms"], marker="o", label="path")
    ax.plot(depth_df["depth"], depth_df["witness_median_ms"], marker="o", label="witness")
    ax.plot(depth_df["depth"], depth_df["prove_median_ms"], marker="o", label="prove")
    ax.plot(depth_df["depth"], depth_df["verify_median_ms"], marker="o", label="verify")
    ax.set_xlabel("Tree depth")
    ax.set_ylabel("Median time (ms)")
    ax.set_title("Anonymous auth cost under different tree depths (bb_each=1)")
    ax.set_xticks(depth_df["depth"].tolist())
    ax.legend()
    save_figure(fig, out_stem)



def plot_nt(fixed_t4: pd.DataFrame, fixed_n6: pd.DataFrame, out_stem: Path):
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))

    ax = axes[0]
    ax.plot(fixed_t4["n"], fixed_t4["avg_recover_ms"], marker="o", label="recover")
    ax.plot(fixed_t4["n"], fixed_t4["avg_trace_run_ms"], marker="o", label="trace-run")
    ax.plot(fixed_t4["n"], fixed_t4["avg_trace_verify_ms"], marker="o", label="trace-verify")
    ax.plot(fixed_t4["n"], fixed_t4["avg_publish_ms"], marker="o", label="publish")
    ax.set_xlabel("n (t=4)")
    ax.set_ylabel("Average time (ms)")
    ax.set_title("Fixed t=4")
    ax.set_xticks(fixed_t4["n"].tolist())
    ax.legend()

    ax = axes[1]
    ax.plot(fixed_n6["t"], fixed_n6["avg_recover_ms"], marker="o", label="recover")
    ax.plot(fixed_n6["t"], fixed_n6["avg_trace_run_ms"], marker="o", label="trace-run")
    ax.plot(fixed_n6["t"], fixed_n6["avg_trace_verify_ms"], marker="o", label="trace-verify")
    ax.plot(fixed_n6["t"], fixed_n6["avg_publish_ms"], marker="o", label="publish")
    ax.set_xlabel("t (n=6)")
    ax.set_ylabel("Average time (ms)")
    ax.set_title("Fixed n=6")
    ax.set_xticks(fixed_n6["t"].tolist())
    ax.legend()

    fig.suptitle("Recovery and tracing cost under different (n,t)")
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(out_stem.with_suffix(".svg"), bbox_inches="tight")
    fig.savefig(out_stem.with_suffix(".pdf"), bbox_inches="tight")
    plt.close(fig)



def plot_combined(depth_df: pd.DataFrame, fixed_t4: pd.DataFrame, fixed_n6: pd.DataFrame, out_stem: Path):
    fig = plt.figure(figsize=(12, 10))
    gs = fig.add_gridspec(2, 2, height_ratios=[1, 1.05])

    ax_top = fig.add_subplot(gs[0, :])
    ax_top.plot(depth_df["depth"], depth_df["path_median_ms"], marker="o", label="path")
    ax_top.plot(depth_df["depth"], depth_df["witness_median_ms"], marker="o", label="witness")
    ax_top.plot(depth_df["depth"], depth_df["prove_median_ms"], marker="o", label="prove")
    ax_top.plot(depth_df["depth"], depth_df["verify_median_ms"], marker="o", label="verify")
    ax_top.set_xlabel("Tree depth")
    ax_top.set_ylabel("Median time (ms)")
    ax_top.set_title("Anonymous auth cost under different tree depths (bb_each=1)")
    ax_top.set_xticks(depth_df["depth"].tolist())
    ax_top.legend()

    ax_left = fig.add_subplot(gs[1, 0])
    ax_left.plot(fixed_t4["n"], fixed_t4["avg_recover_ms"], marker="o", label="recover")
    ax_left.plot(fixed_t4["n"], fixed_t4["avg_trace_run_ms"], marker="o", label="trace-run")
    ax_left.plot(fixed_t4["n"], fixed_t4["avg_trace_verify_ms"], marker="o", label="trace-verify")
    ax_left.plot(fixed_t4["n"], fixed_t4["avg_publish_ms"], marker="o", label="publish")
    ax_left.set_xlabel("n (t=4)")
    ax_left.set_ylabel("Average time (ms)")
    ax_left.set_title("Fixed t=4")
    ax_left.set_xticks(fixed_t4["n"].tolist())
    ax_left.legend()

    ax_right = fig.add_subplot(gs[1, 1])
    ax_right.plot(fixed_n6["t"], fixed_n6["avg_recover_ms"], marker="o", label="recover")
    ax_right.plot(fixed_n6["t"], fixed_n6["avg_trace_run_ms"], marker="o", label="trace-run")
    ax_right.plot(fixed_n6["t"], fixed_n6["avg_trace_verify_ms"], marker="o", label="trace-verify")
    ax_right.plot(fixed_n6["t"], fixed_n6["avg_publish_ms"], marker="o", label="publish")
    ax_right.set_xlabel("t (n=6)")
    ax_right.set_ylabel("Average time (ms)")
    ax_right.set_title("Fixed n=6")
    ax_right.set_xticks(fixed_n6["t"].tolist())
    ax_right.legend()

    fig.suptitle("Depth and (n,t) experiment figures")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(out_stem.with_suffix(".svg"), bbox_inches="tight")
    fig.savefig(out_stem.with_suffix(".pdf"), bbox_inches="tight")
    plt.close(fig)



def main():
    depth_df = extract_depth_summary(TREE_ZIP)
    fixed_t4, fixed_n6, selected = extract_nt_summary(NT_ZIP)

    depth_df.to_csv(OUT_DIR / "tree_depth_summary.csv", index=False)
    fixed_t4.to_csv(OUT_DIR / "nt_fixed_t4_summary.csv", index=False)
    fixed_n6.to_csv(OUT_DIR / "nt_fixed_n6_summary.csv", index=False)
    selected.to_csv(OUT_DIR / "nt_selected_points_summary.csv", index=False)

    tree_stem = OUT_DIR / "tree_depth_cost"
    nt_stem = OUT_DIR / "nt_cost"
    combo_stem = OUT_DIR / "depth_nt_combined"

    plot_tree_depth(depth_df, tree_stem)
    plot_nt(fixed_t4, fixed_n6, nt_stem)
    plot_combined(depth_df, fixed_t4, fixed_n6, combo_stem)

    print("Wrote:")
    print(tree_stem.with_suffix('.svg'))
    print(tree_stem.with_suffix('.pdf'))
    print(nt_stem.with_suffix('.svg'))
    print(nt_stem.with_suffix('.pdf'))
    print(combo_stem.with_suffix('.svg'))
    print(combo_stem.with_suffix('.pdf'))
    print(OUT_DIR / "tree_depth_summary.csv")
    print(OUT_DIR / "nt_fixed_t4_summary.csv")
    print(OUT_DIR / "nt_fixed_n6_summary.csv")
    print(OUT_DIR / "nt_selected_points_summary.csv")


if __name__ == "__main__":
    main()
