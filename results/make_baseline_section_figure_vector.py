from pathlib import Path
import zipfile
import pandas as pd
import matplotlib.pyplot as plt

ZIP_PATH = Path("baseline_100.zip")
OUT_DIR = Path("baseline_section_output")
OUT_DIR.mkdir(exist_ok=True)

AUTH_BB1 = "baseline_100/20260328_141001_G_zk_auth/zk_auth_bb1.csv"
RECOVERY = "baseline_100/20260328_145129_H_zk_recovery_batch/zk_recovery_batch.csv"
METRICS = ["path_fetch_ms", "witness_ms", "prove_ms", "verify_ms"]
LABELS = ["path", "witness", "prove", "verify"]

# Save publication figures as vector graphics.
# SVG works well in modern Word and browsers; PDF is also vector and print-friendly.
VECTOR_FORMATS = ("svg", "pdf")


def save_vector_figure(fig, stem: str) -> list[Path]:
    """Save a matplotlib figure in multiple vector formats."""
    paths = []
    for ext in VECTOR_FORMATS:
        out = OUT_DIR / f"{stem}.{ext}"
        fig.savefig(out, bbox_inches="tight")
        paths.append(out)
    return paths



def main():
    with zipfile.ZipFile(ZIP_PATH) as zf:
        auth = pd.read_csv(zf.open(AUTH_BB1))
        rec = pd.read_csv(zf.open(RECOVERY))

    baseline_vals = [auth[m].median() for m in METRICS]
    recovery_vals = [rec[m].median() for m in METRICS]
    old_invalid = (rec["old_proof_valid"] == 0).mean() * 100.0
    new_valid = (rec["new_proof_valid"] == 1).mean() * 100.0

    written = []

    # Figure A: PCR outcome rates (vector)
    fig, ax = plt.subplots(figsize=(5.2, 4.2))
    ax.bar(["old proof invalid", "new proof valid"], [old_invalid, new_valid])
    ax.set_ylabel("Rate (%)")
    ax.set_ylim(0, 110)
    ax.set_title("PCR outcome rates (100 runs)")
    fig.tight_layout()
    written.extend(save_vector_figure(fig, "pcr_outcome_rates_100runs"))
    plt.close(fig)

    # Figure B: baseline vs recovery cost (vector)
    x = list(range(len(METRICS)))
    width = 0.38
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.bar([i - width / 2 for i in x], baseline_vals, width=width, label="baseline auth")
    ax.bar([i + width / 2 for i in x], recovery_vals, width=width, label="post-recovery auth")
    ax.set_xticks(x)
    ax.set_xticklabels(LABELS)
    ax.set_ylabel("Median time (ms)")
    ax.set_title("Baseline vs post-recovery cost (100 runs)")
    ax.legend()
    fig.tight_layout()
    written.extend(save_vector_figure(fig, "baseline_vs_recovery_cost_100runs"))
    plt.close(fig)

    # Combined figure: draw both panels in one matplotlib canvas so the output stays vector.
    fig, axes = plt.subplots(1, 2, figsize=(12.0, 4.4), constrained_layout=True)

    axes[0].bar(["old proof invalid", "new proof valid"], [old_invalid, new_valid])
    axes[0].set_ylabel("Rate (%)")
    axes[0].set_ylim(0, 110)
    axes[0].set_title("PCR outcome rates (100 runs)")

    axes[1].bar([i - width / 2 for i in x], baseline_vals, width=width, label="baseline auth")
    axes[1].bar([i + width / 2 for i in x], recovery_vals, width=width, label="post-recovery auth")
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(LABELS)
    axes[1].set_ylabel("Median time (ms)")
    axes[1].set_title("Baseline vs post-recovery cost (100 runs)")
    axes[1].legend()

    fig.suptitle("Baseline experiment figure")
    written.extend(save_vector_figure(fig, "baseline_experiment_combined_100runs"))
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
    summary_path = OUT_DIR / "baseline_section_summary.csv"
    summary.to_csv(summary_path, index=False)
    written.append(summary_path)

    print("Wrote:")
    for p in written:
        print(p)


if __name__ == "__main__":
    main()
