import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


def main(base_dir: str, out_dir: str):
    os.makedirs(out_dir, exist_ok=True)

    bb0 = pd.read_csv(os.path.join(base_dir, '20260315_144520_G_zk_auth', 'zk_auth_bb0.csv'))
    bb1 = pd.read_csv(os.path.join(base_dir, '20260315_144520_G_zk_auth', 'zk_auth_bb1.csv'))
    rec = pd.read_csv(os.path.join(base_dir, '20260316_162931_H_zk_recovery_batch', 'zk_recovery_batch.csv'))
    thr = pd.read_csv(os.path.join(base_dir, '20260316_165311_J_ttss_threshold_boundary', 'attempts.csv'))
    trc = pd.read_csv(os.path.join(base_dir, '20260316_183026_K_ttss_trace_cases', 'attempts.csv'))

    # Exp 1
    metrics = ['path_fetch_ms', 'witness_ms', 'prove_ms', 'verify_ms']
    med0 = bb0[metrics].median()
    med1 = bb1[metrics].median()
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    x = np.arange(len(metrics)); w = 0.36
    ax.bar(x - w/2, med0.values, width=w, label='bb_each=0')
    ax.bar(x + w/2, med1.values, width=w, label='bb_each=1')
    ax.set_xticks(x)
    ax.set_xticklabels(['path', 'witness', 'prove', 'verify'])
    ax.set_ylabel('Median time (ms)')
    ax.set_title('Exp.1 End-to-end anonymous auth cost')
    ax.legend(frameon=False, fontsize=9)
    for i, v in enumerate(med0.values):
        ax.text(i - w/2, v + 3, f'{v:.1f}', ha='center', va='bottom', fontsize=8)
    for i, v in enumerate(med1.values):
        ax.text(i + w/2, v + 3, f'{v:.1f}', ha='center', va='bottom', fontsize=8)
    ax.set_ylim(0, max(med1.max(), med0.max()) * 1.22)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, 'fig_exp1_auth_costs.png'), dpi=200, bbox_inches='tight')
    plt.close(fig)

    # Exp 2
    old_fail_rate = (rec['old_proof_valid'] == 0).mean() * 100
    new_valid_rate = (rec['new_proof_valid'] == 1).mean() * 100
    time_meds = rec[['path_fetch_ms', 'witness_ms', 'prove_ms', 'verify_ms']].median()
    fig, axs = plt.subplots(1, 2, figsize=(7.2, 3.2))
    axs[0].bar(['old proof\ninvalid', 'new proof\nvalid'], [old_fail_rate, new_valid_rate])
    axs[0].set_ylim(0, 110)
    axs[0].set_ylabel('Rate (%)')
    axs[0].set_title('Exp.2 PCR outcome rates')
    for i, v in enumerate([old_fail_rate, new_valid_rate]):
        axs[0].text(i, v + 2, f'{v:.0f}%', ha='center', fontsize=9)
    axs[1].bar(['path', 'witness', 'prove', 'verify'], time_meds.values)
    axs[1].set_ylabel('Median time (ms)')
    axs[1].set_title('Exp.2 median latency')
    for i, v in enumerate(time_meds.values):
        axs[1].text(i, v + 3, f'{v:.1f}', ha='center', fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, 'fig_exp2_recovery_rates.png'), dpi=200, bbox_inches='tight')
    plt.close(fig)

    # Exp 3
    grp = thr.groupby('requested_share_count')
    success_rate = grp['recover_ok'].mean() * 100
    recover_ms = grp['recover_ms'].mean().fillna(0)
    share_used = grp['share_count_used'].mean().fillna(0)
    fig, ax1 = plt.subplots(figsize=(6.4, 3.6))
    x = np.arange(len(success_rate.index))
    ax1.bar(x, success_rate.values, width=0.55)
    ax1.set_xticks(x)
    ax1.set_xticklabels([str(int(v)) for v in success_rate.index])
    ax1.set_xlabel('Requested share count')
    ax1.set_ylabel('Recovery success rate (%)')
    ax1.set_ylim(0, 110)
    ax1.set_title('Exp.3 Threshold boundary')
    for i, v in enumerate(success_rate.values):
        ax1.text(i, v + 2, f'{v:.0f}%', ha='center', fontsize=9)
    ax2 = ax1.twinx()
    ax2.plot(x, recover_ms.values, marker='o', linewidth=1.5)
    ax2.set_ylabel('Avg recover time (ms)')
    for i, v in enumerate(recover_ms.values):
        if v > 0:
            ax2.text(i, v + 0.8, f'{v:.1f}', ha='center', fontsize=8)
    for i, v in enumerate(share_used.values):
        if v > 0:
            ax1.text(i, 8, f'used={int(v)}', ha='center', fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, 'fig_exp3_threshold_boundary.png'), dpi=200, bbox_inches='tight')
    plt.close(fig)

    # Exp 4
    trc['label'] = trc.apply(
        lambda r: 'noleak' if r['case_mode'] == 'noleak' else ('badmeta' if r['case_mode'] == 'badmeta' else f"leak {r['expected_leaked_set']}"),
        axis=1,
    )
    summary = trc.groupby('label').agg(
        trace_run_ms=('trace_run_ms', 'mean'),
        trace_verify_ms=('trace_verify_ms', 'mean'),
        publish_ms=('publish_ms', 'mean'),
    ).reset_index()
    order = ['noleak', 'badmeta', 'leak 2,4', 'leak 1,3', 'leak 1,4']
    summary['label'] = pd.Categorical(summary['label'], categories=order, ordered=True)
    summary = summary.sort_values('label')
    fig, ax = plt.subplots(figsize=(7.0, 3.6))
    x = np.arange(len(summary)); w = 0.26
    ax.bar(x - w, summary['trace_run_ms'], width=w, label='trace')
    ax.bar(x, summary['trace_verify_ms'], width=w, label='verify')
    ax.bar(x + w, summary['publish_ms'], width=w, label='publish')
    ax.set_xticks(x)
    ax.set_xticklabels(summary['label'], rotation=15)
    ax.set_ylabel('Average time (ms)')
    ax.set_title('Exp.4 Trace/Verify/Publish across cases')
    ax.legend(frameon=False, fontsize=9)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, 'fig_exp4_trace_cases.png'), dpi=200, bbox_inches='tight')
    plt.close(fig)

    # Combined figures for compact paper layout
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.6))
    x = np.arange(len(metrics)); w = 0.36
    axs[0].bar(x - w/2, med0.values, width=w, label='bb0')
    axs[0].bar(x + w/2, med1.values, width=w, label='bb1')
    axs[0].set_xticks(x); axs[0].set_xticklabels(['path', 'wit', 'prove', 'ver'])
    axs[0].set_title('Exp.1 Auth cost'); axs[0].set_ylabel('Median ms')
    axs[0].legend(frameon=False, fontsize=8)
    axs[1].bar(['old invalid', 'new valid'], [old_fail_rate, new_valid_rate])
    axs[1].set_ylim(0, 110); axs[1].set_ylabel('Rate (%)')
    axs[1].set_title('Exp.2 Recovery-induced invalidation')
    for i, v in enumerate([old_fail_rate, new_valid_rate]):
        axs[1].text(i, v + 2, f'{v:.0f}%', ha='center', fontsize=9)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, 'fig_exp12.png'), dpi=200, bbox_inches='tight')
    plt.close(fig)

    fig, axs = plt.subplots(1, 2, figsize=(10, 3.6))
    x = np.arange(len(success_rate.index))
    axs[0].bar(x, success_rate.values, width=0.55)
    axs[0].set_xticks(x); axs[0].set_xticklabels([str(int(v)) for v in success_rate.index])
    axs[0].set_ylim(0, 110); axs[0].set_xlabel('Requested shares'); axs[0].set_ylabel('Success rate (%)')
    axs[0].set_title('Exp.3 Threshold boundary')
    for i, v in enumerate(success_rate.values):
        axs[0].text(i, v + 2, f'{v:.0f}%', ha='center', fontsize=9)
    x = np.arange(len(summary)); w = 0.26
    axs[1].bar(x - w, summary['trace_run_ms'], width=w, label='trace')
    axs[1].bar(x, summary['trace_verify_ms'], width=w, label='verify')
    axs[1].bar(x + w, summary['publish_ms'], width=w, label='publish')
    axs[1].set_xticks(x); axs[1].set_xticklabels(['noleak', 'badmeta', '2,4', '1,3', '1,4'], rotation=15)
    axs[1].set_ylabel('Avg ms'); axs[1].set_title('Exp.4 Trace cases')
    axs[1].legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, 'fig_exp34.png'), dpi=200, bbox_inches='tight')
    plt.close(fig)


if __name__ == '__main__':
    default_base = '/home/spike/did-e2e/results'
    default_out = '/home/spike/did-e2e/results'
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--base_dir', default=default_base)
    parser.add_argument('--out_dir', default=default_out)
    args = parser.parse_args()
    main(args.base_dir, args.out_dir)
