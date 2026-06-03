#!/usr/bin/env python3
"""Analyze xcav usage logs from ~/.xcav/usage/events.jsonl.

Usage:
    python3 analyze_xcav_usage.py [--jsonl PATH] [--sessions] [--errors] [--workflow] [--all]

Output sections:
    --overview    Event counts, command distribution, ok/fail rate
    --errors      Error type breakdown, top-failing commands
    --workflow    Survey-before-mutation, read-before-edit, redundant blocks
    --sessions    Per-session stats (event count, duration, failure rate)
    --all         All sections (default)
"""

import json
import sys
from collections import Counter, defaultdict
from pathlib import Path


def load(jsonl_path: str) -> list[dict]:
    events = []
    with open(jsonl_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                pass  # skip corrupted lines
    return events


def overview(events: list[dict]):
    total = len(events)
    oks = sum(1 for e in events if e["outcome"]["ok"])
    fails = total - oks
    cmds = Counter(e["cmd"] for e in events)
    cats = Counter(e["cat"] for e in events)
    langs = Counter(
        e.get("target", {}).get("lang", "none") for e in events if e["cmd"] not in ("help", "onboard")
    )

    print("=" * 60)
    print("OVERVIEW")
    print("=" * 60)
    print(f"Total events:  {total}")
    print(f"  Succeeded:   {oks} ({100 * oks / total:.1f}%)" if total else "")
    print(f"  Failed:      {fails} ({100 * fails / total:.1f}%)" if total else "")
    print()
    print("By command:")
    for cmd, n in cmds.most_common():
        print(f"  {cmd:<15s} {n:5d}")
    print()
    print("By category:")
    for cat, n in cats.most_common():
        print(f"  {cat:<15s} {n:5d}")
    print()
    if langs:
        print("By language:")
        for lang, n in langs.most_common():
            print(f"  {lang:<15s} {n:5d}")


def errors(events: list[dict]):
    failures = [e for e in events if not e["outcome"]["ok"]]
    if not failures:
        print("\nNo failures.\n")
        return

    by_cmd = Counter(e["cmd"] for e in failures)
    by_err = Counter(e["outcome"].get("error", "unknown") for e in failures)

    print()
    print("=" * 60)
    print("ERRORS")
    print("=" * 60)
    print(f"Total failures: {len(failures)}")
    print()
    print("By error type:")
    for err, n in by_err.most_common():
        print(f"  {err:<25s} {n:5d}")
    print()
    print("Failing commands:")
    for cmd, n in by_cmd.most_common():
        total_cmd = sum(1 for e in events if e["cmd"] == cmd)
        rate = 100 * n / total_cmd if total_cmd else 0
        print(f"  {cmd:<15s} {n:5d}/{total_cmd:<5d} ({rate:.0f}%)")
    print()
    print("Error × command matrix:")
    matrix = defaultdict(lambda: defaultdict(int))
    for e in failures:
        matrix[e["cmd"]][e["outcome"].get("error", "unknown")] += 1
    err_types = sorted(set(e["outcome"].get("error", "unknown") for e in failures))
    print(f"  {'':15s}", end="")
    for et in err_types:
        print(f" {et:>20s}", end="")
    print()
    for cmd in sorted(matrix):
        print(f"  {cmd:<15s}", end="")
        for et in err_types:
            print(f" {matrix[cmd][et]:20d}", end="")
        print()


def workflow(events: list[dict]):
    print()
    print("=" * 60)
    print("WORKFLOW PATTERNS")
    print("=" * 60)

    # Survey before mutation
    mutations = [e for e in events if e["cat"] == "mutation"]
    if mutations:
        with_survey = sum(
            1
            for e in mutations
            if e.get("ctx", {}).get("prev_cmd", "") in ("blocks", "read")
        )
        print(f"Mutations with prior survey:  {with_survey}/{len(mutations)} "
              f"({100 * with_survey / len(mutations):.0f}%)" if mutations else "")

    # Read before edit
    edits = [e for e in events if e["cmd"] == "edit"]
    if edits:
        with_read = sum(
            1 for e in edits if e.get("ctx", {}).get("prev_cmd", "") == "read"
        )
        print(f"Edits with prior read:        {with_read}/{len(edits)} "
              f"({100 * with_read / len(edits):.0f}%)" if edits else "")

    # Redundant blocks
    blocks_events = [e for e in events if e["cmd"] == "blocks"]
    if blocks_events:
        redundant = sum(
            1 for e in blocks_events if e.get("ctx", {}).get("redundant_blocks", False)
        )
        print(f"Redundant blocks calls:       {redundant}/{len(blocks_events)} "
              f"({100 * redundant / len(blocks_events):.0f}%)" if blocks_events else "")

    # Failure recovery: mutation-fail → undo → retry
    recovery = 0
    for i in range(2, len(events)):
        if (
            events[i - 2]["cat"] == "mutation"
            and not events[i - 2]["outcome"]["ok"]
            and events[i - 1]["cmd"] == "undo"
            and events[i]["cmd"] == events[i - 2]["cmd"]
        ):
            recovery += 1
    mutation_fails = sum(
        1 for e in events if e["cat"] == "mutation" and not e["outcome"]["ok"]
    )
    print(f"Failure→undo→retry chains:    {recovery}"
          + (f"/{mutation_fails} failures" if mutation_fails else ""))

    # Cross-file vs within-file
    cross = sum(1 for e in events if e.get("args", {}).get("cross_file", False))
    has_dst = sum(1 for e in events if e.get("args", {}).get("has_dst", False))
    within = has_dst - cross
    print(f"Cross-file moves/copies:      {cross}")
    print(f"Within-file moves:            {within}")


def sessions(events: list[dict]):
    print()
    print("=" * 60)
    print("SESSIONS")
    print("=" * 60)
    by_session = defaultdict(list)
    for e in events:
        by_session[e["session_id"]].append(e)

    session_stats = []
    for sid, evts in by_session.items():
        pid = sid.split("-")[0]
        total_ms = sum(e["outcome"]["ms"] for e in evts)
        fails = sum(1 for e in evts if not e["outcome"]["ok"])
        cmds = Counter(e["cmd"] for e in evts)
        session_stats.append(
            (sid, pid, len(evts), total_ms, fails, cmds)
        )

    session_stats.sort(key=lambda x: -x[2])  # by event count

    print(f"Total sessions: {len(session_stats)}")
    print()
    print(f"  {'session_id':<45s} {'pid':>6s} {'events':>6s} {'ms':>6s} {'fails':>6s}")
    print(f"  {'-'*45} {'-'*6} {'-'*6} {'-'*6} {'-'*6}")
    for sid, pid, n, ms, fails, cmds in session_stats[:20]:
        sid_short = sid[:44] if len(sid) > 44 else sid
        print(f"  {sid_short:<45s} {pid:>6s} {n:>6d} {ms:>6d} {fails:>6d}")

    if len(session_stats) > 20:
        print(f"  ... and {len(session_stats) - 20} more sessions")


def main():
    jsonl_path = str(Path.home() / ".xcav" / "usage" / "events.jsonl")

    # Handle --jsonl flag
    args = sys.argv[1:]
    if "--jsonl" in args:
        idx = args.index("--jsonl")
        if idx + 1 < len(args):
            jsonl_path = args[idx + 1]
            args.pop(idx)
            args.pop(idx)

    if not Path(jsonl_path).exists():
        print(f"No log file at {jsonl_path}")
        sys.exit(1)

    events = load(jsonl_path)
    if not events:
        print("Log file is empty")
        sys.exit(0)

    events.sort(key=lambda e: e.get("ts", ""))

    flags = set(args) if args else {"--all"}
    if "--all" in flags:
        flags = {"--overview", "--errors", "--workflow", "--sessions"}

    if "--overview" in flags:
        overview(events)
    if "--errors" in flags:
        errors(events)
    if "--workflow" in flags:
        workflow(events)
    if "--sessions" in flags:
        sessions(events)


if __name__ == "__main__":
    main()
