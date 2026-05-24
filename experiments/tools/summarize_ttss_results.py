#!/usr/bin/env python3
import argparse, csv, json
from datetime import datetime
from pathlib import Path
from statistics import mean
from typing import Any, Dict, List, Optional

TABLE_COLUMNS = [
    'table','source_dir','batch_name','status','runs','pass_count','pass_rate','retry_total','retry_per_run',
    'n','t','challenge_count','max_queries','depth',
    'avg_setup_ms','avg_register_zk_ms','avg_share_gen_ms','avg_committee_distribute_ms','avg_register_ttss_meta_ms','avg_leaf_fetch_ms','avg_path_fetch_ms','avg_post_meta_verify_ms','avg_post_setup_state_wait_ms','avg_root_wait_ms','avg_setup_total_inner_ms','avg_register_status_poll_ms','avg_ready_probe_path_leaf_ms','avg_ready_wait_sleep_ms','avg_setup_client_pre_register_ms','avg_setup_field_normalize_ms','avg_setup_local_leaf_compute_ms','avg_setup_root_compare_normalize_ms',
    'p50_setup_ms','p95_setup_ms','avg_recover_ms','p50_recover_ms','p95_recover_ms',
    'avg_rotate_ms','avg_apply_recovery_rotate_ms','avg_rotate_response_parse_ms','avg_invalidate_old_shares_ms','avg_new_share_gen_ms','avg_new_share_distribute_ms','avg_set_new_ttss_meta_ms','avg_post_rotate_state_wait_ms','avg_post_rotate_leaf_fetch_ms','avg_post_rotate_path_fetch_ms','avg_old_new_share_validation_ms','avg_post_rotate_checks_ms','avg_new_root_wait_ms','avg_rotate_total_inner_ms','avg_pre_rotate_prepare_ms','avg_rotate_meta_wait_ms','avg_rotate_ready_probe_ms','avg_rotate_client_pre_rotate_ms','avg_rotate_field_normalize_ms','avg_rotate_local_leaf_compute_ms','avg_rotate_root_compare_normalize_ms',
    'p50_rotate_ms','p95_rotate_ms','avg_load_box_ms','avg_oracle_probe_ms','avg_trace_run_ms','avg_trace_verify_ms','avg_publish_ms','avg_trace_query_count','avg_accused_set_size',
    'old_share_invalid_rate','new_share_active_rate','rotate_ok_rate','root_changed_rate','exact_match_rate','verify_accepted_rate','main_crosscheck_rate','notes'
]


def read_csv(path: Path) -> List[Dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline='', encoding='utf-8') as f:
        return list(csv.DictReader(f))


def to_float(v):
    if v in (None, '', 'null'):
        return None
    try:
        return float(v)
    except Exception:
        return None


def truthy(v) -> bool:
    return str(v).strip().lower() in {'1', 'true', 'yes', 'y', 'pass'}


def avg(xs):
    xs = [float(x) for x in xs if x is not None]
    return round(mean(xs), 3) if xs else ''


def percentile(xs, q):
    xs = sorted(float(x) for x in xs if x is not None)
    if not xs:
        return ''
    if len(xs) == 1:
        return round(xs[0], 3)
    idx = (len(xs) - 1) * q
    lo = int(idx)
    hi = min(lo + 1, len(xs) - 1)
    frac = idx - lo
    return round(xs[lo] * (1 - frac) + xs[hi] * frac, 3)


def pct(n, d):
    return round(n / d, 6) if d else ''


def vals(rows, key):
    out = []
    for r in rows:
        v = to_float(r.get(key))
        if v is not None:
            out.append(v)
    return out


def has_marker_file(d: Path, marker: str) -> bool:
    return d.is_dir() and (d / marker).exists()


def resolve_result_dir(d: Optional[Path], marker: str) -> Optional[Path]:
    """Resolve a result directory, tolerating one-level zip wrapper directories."""
    if d is None:
        return None
    d = d.expanduser()
    if has_marker_file(d, marker):
        return d
    # Common zip pattern: outer_dir/inner_same_name/<marker>
    for child in sorted([p for p in d.iterdir() if p.is_dir()]) if d.exists() and d.is_dir() else []:
        if has_marker_file(child, marker):
            return child
    return d


def latest_dir(base: Path, suffix: str, marker: str) -> Optional[Path]:
    dirs = []
    for p in base.glob(f'*_{suffix}'):
        rp = resolve_result_dir(p, marker)
        if rp and has_marker_file(rp, marker):
            dirs.append(rp)
    return max(dirs, key=lambda p: p.stat().st_mtime) if dirs else None


def root_change_from_row(r: Dict[str, str]) -> int:
    if truthy(r.get('root_changed')):
        return 1
    rb = (r.get('root_before') or '').strip()
    ra = (r.get('root_after') or '').strip()
    if rb and ra and rb.lower() != ra.lower() and truthy(r.get('current_leaf_active')) and truthy(r.get('current_leaf_matches_new')) and not truthy(r.get('current_leaf_matches_old')):
        return 1
    return 0


def base_row(table, d):
    row = {k: '' for k in TABLE_COLUMNS}
    row.update({'table': table, 'source_dir': str(d), 'batch_name': d.name})
    return row


def summarize_recover_dir(d: Path) -> Dict[str, Any]:
    d = resolve_result_dir(d, 'summary.csv')
    summary_rows = read_csv(d / 'summary.csv')
    attempt_rows = read_csv(d / 'attempts.csv')
    runs = len(summary_rows)
    pass_rows = [r for r in summary_rows if truthy(r.get('pass'))]
    pass_count = len(pass_rows)
    retry_total = max(0, len(attempt_rows) - runs)
    row = base_row('recover_batch', d)
    row.update({
        'status': 'pass' if pass_count == runs and runs > 0 else ('partial' if pass_count > 0 else 'fail'),
        'runs': runs, 'pass_count': pass_count, 'pass_rate': pct(pass_count, runs),
        'retry_total': retry_total, 'retry_per_run': round(retry_total / runs, 3) if runs else '',
        'n': summary_rows[0].get('ttss_n', '') if summary_rows else '', 't': summary_rows[0].get('ttss_t', '') if summary_rows else '',
        'avg_setup_ms': avg(vals(pass_rows, 'setup_ms')),
        'avg_register_zk_ms': avg(vals(pass_rows, 'register_zk_ms')),
        'avg_share_gen_ms': avg(vals(pass_rows, 'share_gen_ms')),
        'avg_committee_distribute_ms': avg(vals(pass_rows, 'committee_distribute_ms')),
        'avg_register_ttss_meta_ms': avg(vals(pass_rows, 'register_ttss_meta_ms')),
        'avg_leaf_fetch_ms': avg(vals(pass_rows, 'leaf_fetch_ms')),
        'avg_path_fetch_ms': avg(vals(pass_rows, 'path_fetch_ms')),
        'avg_post_meta_verify_ms': avg(vals(pass_rows, 'post_meta_verify_ms')),
        'avg_post_setup_state_wait_ms': avg(vals(pass_rows, 'post_setup_state_wait_ms')),
        'avg_root_wait_ms': avg(vals(pass_rows, 'root_wait_ms')),
        'avg_setup_total_inner_ms': avg(vals(pass_rows, 'setup_total_inner_ms')),
        'avg_register_status_poll_ms': avg(vals(pass_rows, 'register_status_poll_ms')),
        'avg_ready_probe_path_leaf_ms': avg(vals(pass_rows, 'ready_probe_path_leaf_ms')),
        'avg_ready_wait_sleep_ms': avg(vals(pass_rows, 'ready_wait_sleep_ms')),
        'avg_setup_client_pre_register_ms': avg(vals(pass_rows, 'setup_client_pre_register_ms')),
        'avg_setup_field_normalize_ms': avg(vals(pass_rows, 'setup_field_normalize_ms')),
        'avg_setup_local_leaf_compute_ms': avg(vals(pass_rows, 'setup_local_leaf_compute_ms')),
        'avg_setup_root_compare_normalize_ms': avg(vals(pass_rows, 'setup_root_compare_normalize_ms')),
        'p50_setup_ms': percentile(vals(pass_rows, 'setup_ms'), 0.5), 'p95_setup_ms': percentile(vals(pass_rows, 'setup_ms'), 0.95),
        'avg_recover_ms': avg(vals(pass_rows, 'recover_ms')),
        'p50_recover_ms': percentile(vals(pass_rows, 'recover_ms'), 0.5), 'p95_recover_ms': percentile(vals(pass_rows, 'recover_ms'), 0.95),
        'avg_rotate_ms': avg(vals(pass_rows, 'rotate_ms')),
        'avg_apply_recovery_rotate_ms': avg(vals(pass_rows, 'apply_recovery_rotate_ms')),
        'avg_rotate_response_parse_ms': avg(vals(pass_rows, 'rotate_response_parse_ms')),
        'avg_invalidate_old_shares_ms': avg(vals(pass_rows, 'invalidate_old_shares_ms')),
        'avg_new_share_gen_ms': avg(vals(pass_rows, 'new_share_gen_ms')),
        'avg_new_share_distribute_ms': avg(vals(pass_rows, 'new_share_distribute_ms')),
        'avg_set_new_ttss_meta_ms': avg(vals(pass_rows, 'set_new_ttss_meta_ms')),
        'avg_post_rotate_state_wait_ms': avg(vals(pass_rows, 'post_rotate_state_wait_ms')),
        'avg_post_rotate_leaf_fetch_ms': avg(vals(pass_rows, 'post_rotate_leaf_fetch_ms')),
        'avg_post_rotate_path_fetch_ms': avg(vals(pass_rows, 'post_rotate_path_fetch_ms')),
        'avg_old_new_share_validation_ms': avg(vals(pass_rows, 'old_new_share_validation_ms')),
        'avg_post_rotate_checks_ms': avg(vals(pass_rows, 'post_rotate_checks_ms')),
        'avg_new_root_wait_ms': avg(vals(pass_rows, 'new_root_wait_ms')),
        'avg_rotate_total_inner_ms': avg(vals(pass_rows, 'rotate_total_inner_ms')),
        'avg_pre_rotate_prepare_ms': avg(vals(pass_rows, 'pre_rotate_prepare_ms')),
        'avg_rotate_meta_wait_ms': avg(vals(pass_rows, 'rotate_meta_wait_ms')),
        'avg_rotate_ready_probe_ms': avg(vals(pass_rows, 'rotate_ready_probe_ms')),
        'avg_rotate_client_pre_rotate_ms': avg(vals(pass_rows, 'rotate_client_pre_rotate_ms')),
        'avg_rotate_field_normalize_ms': avg(vals(pass_rows, 'rotate_field_normalize_ms')),
        'avg_rotate_local_leaf_compute_ms': avg(vals(pass_rows, 'rotate_local_leaf_compute_ms')),
        'avg_rotate_root_compare_normalize_ms': avg(vals(pass_rows, 'rotate_root_compare_normalize_ms')),
        'p50_rotate_ms': percentile(vals(pass_rows, 'rotate_ms'), 0.5), 'p95_rotate_ms': percentile(vals(pass_rows, 'rotate_ms'), 0.95),
        'old_share_invalid_rate': pct(sum(truthy(r.get('old_share_invalid_ok')) for r in summary_rows), runs),
        'new_share_active_rate': pct(sum(truthy(r.get('new_share_active_ok')) for r in summary_rows), runs),
        'rotate_ok_rate': pct(sum(truthy(r.get('rotate_ok')) for r in summary_rows), runs),
        'root_changed_rate': pct(sum(root_change_from_row(r) for r in summary_rows), runs),
    })
    return row


def summarize_trace_dir(d: Path) -> Dict[str, Any]:
    d = resolve_result_dir(d, 'summary.csv')
    summary_rows = read_csv(d / 'summary.csv')
    attempt_rows = read_csv(d / 'attempts.csv')
    runs = len(summary_rows)
    pass_rows = [r for r in summary_rows if truthy(r.get('pass'))]
    pass_count = len(pass_rows)
    retry_total = max(0, len(attempt_rows) - runs)
    row = base_row('trace_batch', d)
    row.update({
        'status': 'pass' if pass_count == runs and runs > 0 else ('partial' if pass_count > 0 else 'fail'),
        'runs': runs, 'pass_count': pass_count, 'pass_rate': pct(pass_count, runs),
        'retry_total': retry_total, 'retry_per_run': round(retry_total / runs, 3) if runs else '',
        'n': summary_rows[0].get('ttss_n', '') if summary_rows else '', 't': summary_rows[0].get('ttss_t', '') if summary_rows else '',
        'challenge_count': summary_rows[0].get('challenge_count', '') if summary_rows else '', 'max_queries': summary_rows[0].get('max_queries', '') if summary_rows else '',
        'avg_setup_ms': avg(vals(pass_rows, 'setup_ms')),
        'avg_register_zk_ms': avg(vals(pass_rows, 'register_zk_ms')),
        'avg_share_gen_ms': avg(vals(pass_rows, 'share_gen_ms')),
        'avg_committee_distribute_ms': avg(vals(pass_rows, 'committee_distribute_ms')),
        'avg_register_ttss_meta_ms': avg(vals(pass_rows, 'register_ttss_meta_ms')),
        'avg_leaf_fetch_ms': avg(vals(pass_rows, 'leaf_fetch_ms')),
        'avg_path_fetch_ms': avg(vals(pass_rows, 'path_fetch_ms')),
        'avg_post_meta_verify_ms': avg(vals(pass_rows, 'post_meta_verify_ms')),
        'avg_post_setup_state_wait_ms': avg(vals(pass_rows, 'post_setup_state_wait_ms')),
        'avg_root_wait_ms': avg(vals(pass_rows, 'root_wait_ms')),
        'p50_setup_ms': percentile(vals(pass_rows, 'setup_ms'), 0.5), 'p95_setup_ms': percentile(vals(pass_rows, 'setup_ms'), 0.95),
        'avg_load_box_ms': avg(vals(pass_rows, 'load_box_ms')), 'avg_oracle_probe_ms': avg(vals(pass_rows, 'oracle_probe_ms')),
        'avg_trace_run_ms': avg(vals(pass_rows, 'trace_run_ms')), 'avg_trace_verify_ms': avg(vals(pass_rows, 'trace_verify_ms')),
        'avg_publish_ms': avg(vals(pass_rows, 'publish_ms')), 'avg_trace_query_count': avg(vals(pass_rows, 'trace_query_count')),
        'avg_accused_set_size': avg(vals(pass_rows, 'accused_set_size')),
        'exact_match_rate': pct(sum(truthy(r.get('exact_match')) for r in summary_rows), runs),
        'verify_accepted_rate': pct(sum(truthy(r.get('verify_accepted')) for r in summary_rows), runs),
        'main_crosscheck_rate': pct(sum(truthy(r.get('main_crosscheck_ok')) for r in summary_rows), runs),
    })
    return row


def summarize_scale_dir(d: Path) -> List[Dict[str, Any]]:
    d = resolve_result_dir(d, 'aggregate.csv')
    rows = read_csv(d / 'aggregate.csv')
    out = []
    for r in rows:
        row = base_row(f"scale_{r.get('kind', 'point')}", d)
        for k in ['status', 'runs', 'pass_count', 'pass_rate', 'n', 't', 'challenge_count', 'max_queries', 'depth', 'notes']:
            if k == 'notes':
                row[k] = r.get('err', '')
            else:
                row[k] = r.get(k, '')
        for k in ['avg_setup_ms', 'avg_register_zk_ms', 'avg_share_gen_ms', 'avg_committee_distribute_ms', 'avg_register_ttss_meta_ms', 'avg_leaf_fetch_ms', 'avg_path_fetch_ms', 'avg_post_meta_verify_ms', 'avg_post_setup_state_wait_ms', 'avg_root_wait_ms', 'avg_setup_total_inner_ms', 'avg_register_status_poll_ms', 'avg_ready_probe_path_leaf_ms', 'avg_ready_wait_sleep_ms', 'avg_setup_client_pre_register_ms', 'avg_setup_field_normalize_ms', 'avg_setup_local_leaf_compute_ms', 'avg_setup_root_compare_normalize_ms', 'avg_recover_ms', 'avg_rotate_ms', 'avg_apply_recovery_rotate_ms', 'avg_rotate_response_parse_ms', 'avg_invalidate_old_shares_ms', 'avg_new_share_gen_ms', 'avg_new_share_distribute_ms', 'avg_set_new_ttss_meta_ms', 'avg_post_rotate_state_wait_ms', 'avg_post_rotate_leaf_fetch_ms', 'avg_post_rotate_path_fetch_ms', 'avg_old_new_share_validation_ms', 'avg_post_rotate_checks_ms', 'avg_new_root_wait_ms', 'avg_rotate_total_inner_ms', 'avg_pre_rotate_prepare_ms', 'avg_rotate_meta_wait_ms', 'avg_rotate_ready_probe_ms', 'avg_rotate_client_pre_rotate_ms', 'avg_rotate_field_normalize_ms', 'avg_rotate_local_leaf_compute_ms', 'avg_rotate_root_compare_normalize_ms', 'avg_load_box_ms', 'avg_oracle_probe_ms', 'avg_trace_run_ms', 'avg_trace_verify_ms', 'avg_publish_ms', 'avg_trace_query_count', 'avg_accused_set_size']:
            row[k] = r.get(k, '')
        out.append(row)
    return out


def write_csv(path: Path, rows: List[Dict[str, Any]]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=TABLE_COLUMNS)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, '') for k in TABLE_COLUMNS})


def write_md(path: Path, rows: List[Dict[str, Any]]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ['# TTSS 实验总表', '']
    for r in rows:
        lines.append(f"- {r['table']}: status={r['status']}, runs={r['runs']}, pass={r['pass_count']}/{r['runs']}, source={r['source_dir']}")
    path.write_text('\n'.join(lines), encoding='utf-8')


def is_suite_dir(d: Path) -> bool:
    return d.is_dir() and d.name.endswith('_JKL_suite') and ((d / 'J_ttss_recover_batch').is_dir() or (d / 'K_ttss_trace_batch').is_dir() or (d / 'L_ttss_scale').is_dir())


def latest_suite_dir(results: Path) -> Optional[Path]:
    suites = [p for p in results.glob('*_JKL_suite') if is_suite_dir(p)]
    return max(suites, key=lambda p: p.stat().st_mtime) if suites else None


def resolve_suite_components(suite_dir: Path):
    rd = resolve_result_dir(suite_dir / 'J_ttss_recover_batch', 'summary.csv')
    td = resolve_result_dir(suite_dir / 'K_ttss_trace_batch', 'summary.csv')
    sd = resolve_result_dir(suite_dir / 'L_ttss_scale', 'aggregate.csv')
    return (
        rd if rd and (rd / 'summary.csv').exists() else None,
        td if td and (td / 'summary.csv').exists() else None,
        sd if sd and (sd / 'aggregate.csv').exists() else None,
    )


def collect_rows_from_suite(suite_dir: Path) -> List[Dict[str, Any]]:
    rows = []
    rd, td, sd = resolve_suite_components(suite_dir)
    if rd:
        rows.append(summarize_recover_dir(rd))
    if td:
        rows.append(summarize_trace_dir(td))
    if sd:
        rows.extend(summarize_scale_dir(sd))
    return rows


def default_out_dir(results: Path) -> Path:
    return results / (datetime.now().strftime('%Y%m%d_%H%M%S') + '_TTSS_overall_report')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=str(Path.home() / 'did-e2e'))
    ap.add_argument('--suite-dir')
    ap.add_argument('--recover-dir')
    ap.add_argument('--trace-dir')
    ap.add_argument('--scale-dir')
    ap.add_argument('--out-dir')
    ap.add_argument('--all', action='store_true')
    args = ap.parse_args()

    root = Path(args.root).expanduser()
    results = root / 'results'
    rows = []
    out_dir = Path(args.out_dir).expanduser() if args.out_dir else default_out_dir(results)

    if args.all:
        seen = set()
        for d in sorted(results.glob('*_J_ttss_recover_batch')):
            rd = resolve_result_dir(d, 'summary.csv')
            if rd and (rd / 'summary.csv').exists() and str(rd.resolve()) not in seen:
                rows.append(summarize_recover_dir(rd))
                seen.add(str(rd.resolve()))
        for d in sorted(results.glob('*_K_ttss_trace_batch')):
            td = resolve_result_dir(d, 'summary.csv')
            if td and (td / 'summary.csv').exists() and str(td.resolve()) not in seen:
                rows.append(summarize_trace_dir(td))
                seen.add(str(td.resolve()))
        for d in sorted(results.glob('*_L_ttss_scale')):
            sd = resolve_result_dir(d, 'aggregate.csv')
            if sd and (sd / 'aggregate.csv').exists() and str(sd.resolve()) not in seen:
                rows.extend(summarize_scale_dir(sd))
                seen.add(str(sd.resolve()))
        for s in sorted(results.glob('*_JKL_suite')):
            if is_suite_dir(s):
                suite_rows = collect_rows_from_suite(s)
                rows.extend(suite_rows)
    else:
        suite_dir = Path(args.suite_dir).expanduser() if args.suite_dir else latest_suite_dir(results)
        if suite_dir and is_suite_dir(suite_dir):
            rows.extend(collect_rows_from_suite(suite_dir))
        else:
            rd = resolve_result_dir(Path(args.recover_dir), 'summary.csv') if args.recover_dir else latest_dir(results, 'J_ttss_recover_batch', 'summary.csv')
            td = resolve_result_dir(Path(args.trace_dir), 'summary.csv') if args.trace_dir else latest_dir(results, 'K_ttss_trace_batch', 'summary.csv')
            sd = resolve_result_dir(Path(args.scale_dir), 'aggregate.csv') if args.scale_dir else latest_dir(results, 'L_ttss_scale', 'aggregate.csv')
            if rd and (rd / 'summary.csv').exists():
                rows.append(summarize_recover_dir(rd))
            if td and (td / 'summary.csv').exists():
                rows.append(summarize_trace_dir(td))
            if sd and (sd / 'aggregate.csv').exists():
                rows.extend(summarize_scale_dir(sd))
    if not rows:
        raise SystemExit('no result dirs found')
    write_csv(out_dir / 'experiment_master_table.csv', rows)
    write_csv(out_dir / 'overall_summary.csv', rows)
    write_csv(out_dir / 'recover_batch_overview.csv', [r for r in rows if r['table'] == 'recover_batch'])
    write_csv(out_dir / 'trace_batch_overview.csv', [r for r in rows if r['table'] == 'trace_batch'])
    write_csv(out_dir / 'scale_overview.csv', [r for r in rows if r['table'].startswith('scale_')])
    write_md(out_dir / 'overall_summary.md', rows)
    print(json.dumps({'ok': 1, 'out_dir': str(out_dir), 'master_csv': str(out_dir / 'experiment_master_table.csv'), 'overall_csv': str(out_dir / 'overall_summary.csv')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
