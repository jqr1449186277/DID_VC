#!/usr/bin/env python3
"""
Summarize sidecar chain/communication logs.

Expected inputs:
  --gas   path to gas_events.jsonl produced by gas_monitor.js
  --http  path to http_events.jsonl produced by http_byte_proxy.py
  --out-dir output directory for CSV summaries

Outputs:
  chain_summary.csv
  comm_summary.csv
  chain_comm_summary.csv

This version is compatible with the newer gas monitor format where real chain
transactions are recorded as JSONL rows with:
  type = "tx_hit"
  stage
  gasUsed
  status
and remains permissive toward older formats.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from collections import defaultdict
from typing import Any, Dict, Iterable, List, Tuple


def eprint(*args: Any, **kwargs: Any) -> None:
    print(*args, file=sys.stderr, **kwargs)


def ensure_dir(path: str) -> None:
    os.makedirs(path, exist_ok=True)


def read_jsonl(path: str) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    if not path or not os.path.exists(path):
        eprint(f"[summarize_chain_comm] WARN missing file: {path}")
        return rows

    with open(path, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                if isinstance(obj, dict):
                    rows.append(obj)
                else:
                    eprint(f"[summarize_chain_comm] WARN non-object JSON ignored at {path}:{lineno}")
            except json.JSONDecodeError as exc:
                eprint(f"[summarize_chain_comm] WARN bad JSON ignored at {path}:{lineno}: {exc}")
    return rows


def parse_int(value: Any, default: int = 0) -> int:
    if value is None:
        return default
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    s = str(value).strip()
    if not s:
        return default
    try:
        if s.startswith(("0x", "0X")):
            return int(s, 16)
        return int(s)
    except Exception:
        return default


# -------------------------
# Chain (gas) summarization
# -------------------------

def classify_chain_row(row: Dict[str, Any]) -> Tuple[bool, str]:
    """
    Returns (countable, stage).

    Count real on-chain tx records only. Preferred new-format indicator is
    type == "tx_hit". For backward compatibility, allow rows that include a
    stage and gasUsed/txHash combination.
    """
    row_type = str(row.get("type", "")).strip()
    stage = str(row.get("stage", "")).strip() or "other"

    if row_type == "tx_hit":
        return True, stage

    # Backward compatibility path: if it looks like a tx record, count it.
    has_tx_identity = any(k in row for k in ("txHash", "transactionHash"))
    has_gas = any(k in row for k in ("gasUsed", "gas"))
    if stage and has_tx_identity and has_gas:
        return True, stage

    return False, stage


def extract_gas_value(row: Dict[str, Any]) -> int:
    if "gasUsed" in row:
        return parse_int(row.get("gasUsed"), 0)
    if "gas" in row:
        return parse_int(row.get("gas"), 0)
    return 0


def extract_status_value(row: Dict[str, Any]) -> int:
    # New format uses numeric status from receipt: 1 success, 0 failure
    if "status" in row:
        return parse_int(row.get("status"), 1)
    # Fallback: treat missing status as success if gas record exists
    return 1


def summarize_chain(gas_rows: Iterable[Dict[str, Any]]) -> Tuple[List[List[Any]], Dict[str, Any]]:
    agg: Dict[str, Dict[str, int]] = defaultdict(lambda: {
        "tx_count": 0,
        "gas_sum": 0,
        "failed_tx_count": 0,
    })

    meta = {
        "input_rows": 0,
        "counted_rows": 0,
        "ignored_rows": 0,
        "fatal_rows": 0,
        "monitor_error_rows": 0,
        "preflight_error_rows": 0,
        "started_rows": 0,
        "resolved_rows": 0,
    }

    for row in gas_rows:
        meta["input_rows"] += 1
        row_type = str(row.get("type", "")).strip()
        if row_type == "fatal":
            meta["fatal_rows"] += 1
        elif row_type == "monitor_error":
            meta["monitor_error_rows"] += 1
        elif row_type == "preflight_error":
            meta["preflight_error_rows"] += 1
        elif row_type == "monitor_started":
            meta["started_rows"] += 1
        elif row_type == "target_contract_resolved":
            meta["resolved_rows"] += 1

        countable, stage = classify_chain_row(row)
        if not countable:
            meta["ignored_rows"] += 1
            continue

        gas_used = extract_gas_value(row)
        status = extract_status_value(row)

        agg[stage]["tx_count"] += 1
        agg[stage]["gas_sum"] += gas_used
        if status != 1:
            agg[stage]["failed_tx_count"] += 1
        meta["counted_rows"] += 1

    out: List[List[Any]] = []
    total_tx = 0
    total_gas = 0
    total_failed = 0

    for stage in sorted(agg.keys()):
        tx_count = agg[stage]["tx_count"]
        gas_sum = agg[stage]["gas_sum"]
        failed = agg[stage]["failed_tx_count"]
        out.append([stage, tx_count, gas_sum, failed])
        total_tx += tx_count
        total_gas += gas_sum
        total_failed += failed

    out.append(["TOTAL", total_tx, total_gas, total_failed])
    return out, meta


# -----------------------------
# Communication summarization
# -----------------------------

def http_stage(path: str) -> str:
    p = (path or "").strip()
    if p == "/registerZk":
        return "register"
    if p in ("/root", "/path", "/leaf", "/registerStatus", "/ttssMeta"):
        return "state_fetch"
    if p == "/registerTTSSMeta":
        return "ttss_meta"
    if p in ("/applyRecoveryRotateZk", "/applyRecoveryRotateTTSS"):
        return "recover_rotate"
    if p == "/shareForRecover":
        return "recover_share"
    if p == "/shareForTrace":
        return "trace_share"
    if p == "/publishTrace":
        return "trace_publish"
    if p == "/verify" or p.endswith("/verify"):
        return "verify_rpc"
    return "other"


def summarize_comm(http_rows: Iterable[Dict[str, Any]]) -> Tuple[List[List[Any]], Dict[str, Any]]:
    agg: Dict[str, Dict[str, int]] = defaultdict(lambda: {
        "msg_count": 0,
        "upload": 0,
        "download": 0,
        "error_count": 0,
    })

    meta = {
        "input_rows": 0,
        "counted_rows": 0,
        "ignored_rows": 0,
        "total_error_count": 0,
    }

    for row in http_rows:
        meta["input_rows"] += 1

        # Ignore non-request diagnostic rows if any appear later.
        if str(row.get("type", "")).strip() in {"monitor_started", "fatal", "proxy_error"}:
            meta["ignored_rows"] += 1
            continue

        stage = http_stage(str(row.get("path", "")))
        status = parse_int(row.get("status"), 0)
        req_bytes = parse_int(row.get("req_bytes"), 0)
        resp_bytes = parse_int(row.get("resp_bytes"), 0)
        is_error = 1 if (status >= 400 or status == 0 or row.get("error")) else 0

        agg[stage]["msg_count"] += 1
        agg[stage]["upload"] += req_bytes
        agg[stage]["download"] += resp_bytes
        agg[stage]["error_count"] += is_error
        meta["counted_rows"] += 1
        meta["total_error_count"] += is_error

    out: List[List[Any]] = []
    total_count = 0
    total_up = 0
    total_down = 0
    total_err = 0

    for stage in sorted(agg.keys()):
        msg_count = agg[stage]["msg_count"]
        upload = agg[stage]["upload"]
        download = agg[stage]["download"]
        errors = agg[stage]["error_count"]
        out.append([stage, msg_count, upload, download, upload + download, errors])
        total_count += msg_count
        total_up += upload
        total_down += download
        total_err += errors

    out.append(["TOTAL", total_count, total_up, total_down, total_up + total_down, total_err])
    return out, meta


# -------------------------
# CSV writing
# -------------------------

def write_csv(path: str, header: List[str], rows: List[List[Any]]) -> None:
    ensure_dir(os.path.dirname(path) or ".")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


# -------------------------
# Main
# -------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--gas", required=True, help="path to gas_events.jsonl")
    ap.add_argument("--http", required=True, help="path to http_events.jsonl")
    ap.add_argument("--out-dir", required=True, help="directory to write CSV outputs")
    args = ap.parse_args()

    ensure_dir(args.out_dir)

    gas_rows = read_jsonl(args.gas)
    http_rows = read_jsonl(args.http)

    chain_summary, chain_meta = summarize_chain(gas_rows)
    comm_summary, comm_meta = summarize_comm(http_rows)

    chain_csv = os.path.join(args.out_dir, "chain_summary.csv")
    comm_csv = os.path.join(args.out_dir, "comm_summary.csv")
    merged_csv = os.path.join(args.out_dir, "chain_comm_summary.csv")

    write_csv(
        chain_csv,
        ["stage", "tx_count", "gas_sum", "failed_tx_count"],
        chain_summary,
    )

    write_csv(
        comm_csv,
        ["stage", "msg_count", "upload_bytes", "download_bytes", "total_bytes", "error_count"],
        comm_summary,
    )

    merged_rows = [
        ["chain_input_rows", chain_meta["input_rows"]],
        ["chain_counted_rows", chain_meta["counted_rows"]],
        ["chain_ignored_rows", chain_meta["ignored_rows"]],
        ["chain_fatal_rows", chain_meta["fatal_rows"]],
        ["chain_monitor_error_rows", chain_meta["monitor_error_rows"]],
        ["chain_preflight_error_rows", chain_meta["preflight_error_rows"]],
        ["http_input_rows", comm_meta["input_rows"]],
        ["http_counted_rows", comm_meta["counted_rows"]],
        ["http_ignored_rows", comm_meta["ignored_rows"]],
        ["http_total_error_count", comm_meta["total_error_count"]],
        ["total_chain_gas", next((r[2] for r in chain_summary if r[0] == "TOTAL"), 0)],
        ["total_chain_tx", next((r[1] for r in chain_summary if r[0] == "TOTAL"), 0)],
        ["total_chain_failed_tx", next((r[3] for r in chain_summary if r[0] == "TOTAL"), 0)],
        ["total_comm_bytes", next((r[4] for r in comm_summary if r[0] == "TOTAL"), 0)],
        ["total_comm_msgs", next((r[1] for r in comm_summary if r[0] == "TOTAL"), 0)],
        ["total_comm_errors", next((r[5] for r in comm_summary if r[0] == "TOTAL"), 0)],
    ]

    write_csv(merged_csv, ["metric", "value"], merged_rows)

    eprint(f"[summarize_chain_comm] wrote {chain_csv}")
    eprint(f"[summarize_chain_comm] wrote {comm_csv}")
    eprint(f"[summarize_chain_comm] wrote {merged_csv}")
    eprint(
        "[summarize_chain_comm] summary: "
        f"chain_tx={next((r[1] for r in chain_summary if r[0] == 'TOTAL'), 0)} "
        f"chain_gas={next((r[2] for r in chain_summary if r[0] == 'TOTAL'), 0)} "
        f"http_msgs={next((r[1] for r in comm_summary if r[0] == 'TOTAL'), 0)} "
        f"http_bytes={next((r[4] for r in comm_summary if r[0] == 'TOTAL'), 0)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
