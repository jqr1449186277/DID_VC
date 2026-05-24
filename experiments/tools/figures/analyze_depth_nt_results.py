from pathlib import Path
import zipfile
import re
import pandas as pd
import matplotlib.pyplot as plt
from PIL import Image, ImageDraw

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

def plot_tree_depth(depth_df: pd.DataFrame, out_path: Path):
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
    fig.tight_layout()
    fig.savefig(out_path, dpi=200)
    plt.close(fig)

def plot_nt(fixed_t4: pd.DataFrame, fixed_n6: pd.DataFrame, out_path: Path):
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
    fig.tight_layout()
    fig.savefig(out_path, dpi=200)
    plt.close(fig)

def make_combined(tree_img: Path, nt_img: Path, out_path: Path):
    top = Image.open(tree_img).convert("RGB")
    bottom = Image.open(nt_img).convert("RGB")

    target_w = max(top.width, bottom.width)

    def fit_w(img, w):
        if img.width == w:
            return img
        new_h = int(img.height * w / img.width)
        return img.resize((w, new_h))

    top = fit_w(top, target_w)
    bottom = fit_w(bottom, target_w)

    gap = 24
    title_h = 52
    combo = Image.new("RGB", (target_w, title_h + top.height + gap + bottom.height), "white")
    draw = ImageDraw.Draw(combo)
    draw.text((16, 14), "Depth and (n,t) experiment figures", fill="black")
    combo.paste(top, (0, title_h))
    combo.paste(bottom, (0, title_h + top.height + gap))
    combo.save(out_path)

def main():
    depth_df = extract_depth_summary(TREE_ZIP)
    fixed_t4, fixed_n6, selected = extract_nt_summary(NT_ZIP)

    depth_df.to_csv(OUT_DIR / "tree_depth_summary.csv", index=False)
    fixed_t4.to_csv(OUT_DIR / "nt_fixed_t4_summary.csv", index=False)
    fixed_n6.to_csv(OUT_DIR / "nt_fixed_n6_summary.csv", index=False)
    selected.to_csv(OUT_DIR / "nt_selected_points_summary.csv", index=False)

    tree_img = OUT_DIR / "tree_depth_cost.png"
    nt_img = OUT_DIR / "nt_cost.png"
    combo_img = OUT_DIR / "depth_nt_combined.png"

    plot_tree_depth(depth_df, tree_img)
    plot_nt(fixed_t4, fixed_n6, nt_img)
    make_combined(tree_img, nt_img, combo_img)

    print("Wrote:")
    print(tree_img)
    print(nt_img)
    print(combo_img)
    print(OUT_DIR / "tree_depth_summary.csv")
    print(OUT_DIR / "nt_fixed_t4_summary.csv")
    print(OUT_DIR / "nt_fixed_n6_summary.csv")
    print(OUT_DIR / "nt_selected_points_summary.csv")

if __name__ == "__main__":
    main()