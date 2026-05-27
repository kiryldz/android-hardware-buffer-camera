#!/usr/bin/env python3
"""
Compare a benchmark results.json against a per-GPU baseline, enforce the
tolerance gates defined in benchmark/gates.yaml, and write a GitHub Actions
step summary (when $GITHUB_STEP_SUMMARY is set).

Usage:
    scripts/compare-baseline.py BASELINE.json RESULTS.json [--gates GATES.yaml]

Exit codes:
    0  All gated metrics within tolerance.
    1  At least one regression beyond tolerance.
    2  At least one improvement beyond tolerance — regenerate the baseline.
       (The proposed new baseline JSON is printed to stdout for easy copy-paste.)

A metric is a (stage, stat) pair such as "dz.frame_e2e.gl.p90".
Counter metrics are keyed as "dz.dropped_frames.gl".
"""

import argparse
import json
import os
import sys

try:
    import yaml
except ImportError:
    yaml = None


# ── Simple YAML loader (avoids adding PyYAML as a hard dep in CI) ─────────────

def _parse_yaml(text: str) -> dict:
    """Minimal YAML parser — handles only the scalar/list/dict subset used in
    gates.yaml. Falls back to PyYAML when available."""
    if yaml is not None:
        return yaml.safe_load(text)
    # Hand-rolled: good enough for our controlled file.
    root: dict = {}
    current_key: str | None = None
    current_list: list | None = None
    for raw in text.splitlines():
        line = raw.rstrip()
        if not line or line.lstrip().startswith("#"):
            continue
        if not line.startswith(" "):
            if ":" in line:
                k, _, v = line.partition(":")
                v = v.strip()
                if v:
                    try:
                        root[k.strip()] = int(v)
                    except ValueError:
                        root[k.strip()] = v
                else:
                    root[k.strip()] = {}
                current_key = k.strip()
                current_list = None
        else:
            stripped = line.lstrip()
            indent = len(line) - len(stripped)
            if current_key is None:
                continue
            if stripped.startswith("- "):
                val = stripped[2:].strip()
                if not isinstance(root.get(current_key), list):
                    root[current_key] = []
                root[current_key].append(val)
            elif ":" in stripped:
                k2, _, v2 = stripped.partition(":")
                v2 = v2.strip()
                if isinstance(root.get(current_key), dict):
                    try:
                        root[current_key][k2.strip()] = int(v2)
                    except ValueError:
                        root[current_key][k2.strip()] = v2
    return root


def load_gates(path: str) -> dict:
    with open(path) as f:
        raw = _parse_yaml(f.read())

    gates: dict[str, tuple[str, float | None]] = {}  # metric_key -> (tier, tolerance_pct)

    for tier in ("tight", "loose"):
        block = raw.get(tier, {})
        tol = float(block.get("tolerance_pct", 5 if tier == "tight" else 10))
        for m in block.get("metrics", []):
            gates[m] = (tier, tol)

    for m in raw.get("watch", {}).get("metrics", []) if isinstance(raw.get("watch"), dict) else []:
        gates[m] = ("watch", None)

    for m in (raw.get("skip") or []):
        gates[m] = ("skip", None)

    return gates


# ── Value extraction ──────────────────────────────────────────────────────────

def extract_values(data: dict) -> dict[str, float]:
    """Flatten stages + counters into metric_key -> mean."""
    out: dict[str, float] = {}
    for stage, stats in data.get("stages", {}).items():
        for stat, v in stats.items():
            if stat in ("n", "stdev", "cv_pct", "values"):
                continue
            if isinstance(v, (int, float)):
                out[f"{stage}.{stat}"] = float(v)
            elif isinstance(v, dict) and "mean" in v:
                out[f"{stage}.{stat}"] = float(v["mean"])
    for name, v in data.get("counters", {}).items():
        if isinstance(v, (int, float)):
            out[name] = float(v)
        elif isinstance(v, dict) and "mean" in v:
            out[name] = float(v["mean"])
    return out


# ── Comparison ────────────────────────────────────────────────────────────────

STATUS_PASS       = "✅ pass"
STATUS_REGRESSION = "❌ REGRESSION"
STATUS_IMPROVED   = "⚠️  IMPROVED — regen baseline"
STATUS_WATCH      = "👁  watch"
STATUS_SKIP       = "—"
STATUS_MISSING    = "❓ missing"


def compare(baseline: dict, results: dict, gates: dict) -> tuple[int, list[dict]]:
    """Returns (exit_code, rows) where rows drive the markdown table."""
    b_vals = extract_values(baseline)
    r_vals = extract_values(results)

    all_keys = sorted(set(b_vals) | set(r_vals))
    rows = []
    has_regression = False
    has_improvement = False

    for key in all_keys:
        tier, tol = gates.get(key, ("watch", None))
        if tier == "skip":
            continue

        b = b_vals.get(key)
        r = r_vals.get(key)

        if b is None or r is None:
            rows.append({"key": key, "baseline": b, "observed": r, "delta_pct": None,
                         "tier": tier, "status": STATUS_MISSING})
            continue

        if b == 0.0:
            delta_pct = 0.0 if r == 0.0 else float("inf")
        else:
            delta_pct = (r - b) / b * 100.0

        if tier == "watch" or tol is None:
            status = STATUS_WATCH
        elif abs(delta_pct) <= tol:
            status = STATUS_PASS
        elif delta_pct > tol:
            status = STATUS_REGRESSION
            has_regression = True
        else:
            status = STATUS_IMPROVED
            has_improvement = True

        rows.append({
            "key": key, "baseline": b, "observed": r,
            "delta_pct": delta_pct, "tier": tier, "status": status,
        })

    exit_code = 0
    if has_regression:
        exit_code = 1
    elif has_improvement:
        exit_code = 2
    return exit_code, rows


# ── Output ────────────────────────────────────────────────────────────────────

def fmt_ms(v: float | None) -> str:
    return f"{v:.3f}" if v is not None else "—"


def fmt_delta(v: float | None) -> str:
    if v is None:
        return "—"
    if v == float("inf"):
        return "+∞%"
    return f"{v:+.1f}%"


def render_markdown(rows: list[dict], exit_code: int, baseline_path: str,
                    results_path: str, ftl_mismatch: str | None) -> str:
    lines = ["## Frame-latency benchmark results", ""]

    if ftl_mismatch:
        lines += [f"> ⚠️ {ftl_mismatch}", ""]

    if exit_code == 0:
        lines.append("> ✅ All gated metrics within tolerance.")
    elif exit_code == 1:
        lines.append("> ❌ **Regression detected** — fix the performance issue before merging.")
    else:
        lines.append("> ⚠️ **Improvement detected** — run `scripts/aggregate-traces.py` locally "
                     "and commit the updated `baseline-<gpu>.json` before merging.")

    lines += [
        "",
        f"Baseline: `{baseline_path}` | Results: `{results_path}`",
        "",
        "| metric | tier | baseline (ms) | observed (ms) | Δ% | status |",
        "|--------|------|--------------|--------------|-----|--------|",
    ]
    for r in rows:
        if r["status"] == STATUS_SKIP:
            continue
        lines.append(
            f"| `{r['key']}` | {r['tier']} "
            f"| {fmt_ms(r['baseline'])} | {fmt_ms(r['observed'])} "
            f"| {fmt_delta(r['delta_pct'])} | {r['status']} |"
        )
    return "\n".join(lines) + "\n"


def proposed_baseline_json(results: dict) -> str:
    """Strip _placeholder fields and pretty-print for the step summary."""
    clean = {k: v for k, v in results.items() if not k.startswith("_")}
    return json.dumps(clean, indent=2)


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_gates = os.path.join(repo_root, "benchmark", "gates.yaml")

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("baseline_json")
    parser.add_argument("results_json")
    parser.add_argument("--gates", default=default_gates)
    args = parser.parse_args()

    with open(args.baseline_json) as f:
        baseline = json.load(f)
    with open(args.results_json) as f:
        results = json.load(f)

    # Placeholder baselines always "improve" — that's intentional on first run.
    if baseline.get("_placeholder"):
        print("Baseline is a placeholder — treating all metrics as improvements.", file=sys.stderr)
        print("Copy the proposed JSON below into the baseline file and re-push.", file=sys.stderr)

    # Guard against silent FTL pool swaps.
    ftl_mismatch: str | None = None
    b_model = baseline.get("ftl_model_id")
    r_model = results.get("ftl_model_id")
    if b_model and r_model and b_model != "unknown" and r_model != "unknown" and b_model != r_model:
        ftl_mismatch = (
            f"FTL model mismatch: baseline captured on `{b_model}`, "
            f"this run used `{r_model}`. Results are not comparable."
        )
        print(f"error: {ftl_mismatch}", file=sys.stderr)
        sys.exit(3)

    gates = load_gates(args.gates)
    exit_code, rows = compare(baseline, results, gates)

    # Console table.
    print(f"\n{'metric':<40} {'tier':<6} {'baseline':>10} {'observed':>10} {'Δ%':>8}  status")
    print("-" * 90)
    for r in rows:
        print(
            f"{r['key']:<40} {r['tier']:<6} "
            f"{fmt_ms(r['baseline']):>10} {fmt_ms(r['observed']):>10} "
            f"{fmt_delta(r['delta_pct']):>8}  {r['status']}"
        )

    md = render_markdown(rows, exit_code, args.baseline_json, args.results_json, ftl_mismatch)

    step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if step_summary:
        with open(step_summary, "a") as f:
            f.write(md)
        if exit_code == 2:
            with open(step_summary, "a") as f:
                f.write("\n### Proposed updated baseline\n\n```json\n")
                f.write(proposed_baseline_json(results))
                f.write("\n```\n")
    else:
        print("\n" + md)
        if exit_code == 2:
            print("### Proposed updated baseline\n")
            print(proposed_baseline_json(results))

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
