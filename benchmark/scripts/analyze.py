#!/usr/bin/env python3
"""
analyze.py - Aggregates the benchmark CSVs and produces the comparison between
the baseline Slim-Tree and the Query Gravity Slim-Tree.

Deliberately depends on the Python standard library ONLY (no pandas, no numpy,
no matplotlib), so it runs on a bare Python install with no pip step and no
network access. Charts are emitted as SVG, which every browser, VS Code and
GitHub can render.

Outputs (under benchmark/results/):
    summary_by_config.csv   one row per (version, dataset, protocol, query, k)
    comparison.csv          baseline vs query gravity, with percentage changes
    SUMMARY.md              human-readable tables and verdicts
    charts/*.svg            each metric as a function of k

Percentage change is always
    percentage_change = ((new_value - old_value) / old_value) * 100
with new = Query Gravity and old = baseline. For every metric reported here
LOWER IS BETTER, so a NEGATIVE percentage means Query Gravity IMPROVED it.

Usage:
    py benchmark/scripts/analyze.py [--results DIR]
"""

import argparse
import csv
import glob
import math
import os
import statistics
import sys
from collections import defaultdict

# Metrics compared between versions. Every one of them is "lower is better",
# which is what makes the sign convention in the report unambiguous.
COMPARED_METRICS = [
    ("mean_query_time_ms", "Mean query time (ms)"),
    ("median_query_time_ms", "Median query time (ms)"),
    ("mean_distance_calculations", "Distance calculations per query"),
    ("mean_disk_accesses", "Disk page reads per query"),
    ("total_query_time_ms", "Total query time (ms)"),
    ("total_distance_calculations", "Total distance calculations"),
    ("total_disk_accesses", "Total disk page reads"),
    ("construction_time_ms", "Index construction time (ms)"),
    ("optimize_time_ms", "Optimize() / Slim-Down time (ms)"),
    ("index_size_bytes", "Index file size (bytes)"),
]

# Wall-clock metrics carry a few percent of run-to-run noise on a normal desktop.
# The library's counters, by contrast, are exact and perfectly reproducible. So a
# small timing delta must NOT be reported as a win or a loss: verdicts for these
# metrics are judged against the measured noise floor instead of a fixed
# tolerance. (Without this, an early run of this benchmark reported Query Gravity
# as ~5% "faster" in a configuration where both trees were byte-identical.)
TIME_METRICS = {
    "mean_query_time_ms", "median_query_time_ms", "total_query_time_ms",
    "construction_time_ms", "optimize_time_ms",
}

# Columns whose per-repetition values are collapsed with the MEDIAN rather than
# the mean. A single OS scheduling hiccup inside a repetition inflates that
# repetition's *mean* query time by 50-80% while leaving its median untouched;
# taking the median of the per-repetition medians cut the run-to-run relative
# spread of the headline timing metric from ~24% to ~3% on this workload.
ROBUST_MEDIAN_COLS = {"median_query_time_ms", "p95_query_time_ms"}

# Headline metrics. Timing uses the robust median; the two counters are exact.
CHART_METRICS = [
    ("median_query_time_ms", "Median query time (ms)"),
    ("mean_distance_calculations", "Distance calculations per query"),
    ("mean_disk_accesses", "Disk page reads per query"),
]

# Identifies one experimental cell; version is compared *across* these.
CONFIG_KEYS = ("dataset", "protocol", "query_dist", "query_type", "k", "radius")

# What each (protocol, version) pair actually exercises. Spelled out because the
# protocol names alone hide the fact that "baseline + train-optimize" IS the
# original Slim-Down: the baseline commit has no hotspot recording at all, so the
# training queries are read-only no-ops there and the tree is reorganised by the
# unmodified `distance <= dst->GetMinimumRadius()` test.
ARM_NOTES = [
    ("baseline", "no-optimize",
     "Original Slim-Tree, Slim-Down never run (what the stock test app does)."),
    ("baseline", "train-optimize",
     "ORIGINAL SLIM-DOWN: Optimize() reorganises points using the unmodified "
     "`distance <= GetMinimumRadius()` test, which can only shrink radii. The "
     "training queries are read-only no-ops for this version."),
    ("querygravity", "no-optimize",
     "Query Gravity, Slim-Down never run. Tree is structurally identical to the "
     "baseline, so this isolates the cost of AddQueryHotspot() alone."),
    ("querygravity", "train-optimize",
     "QUERY GRAVITY SLIM-DOWN: the swap test becomes "
     "`distance <= radius + heat*0.1*radius`, so hot regions are allowed to GROW."),
]

BASELINE = "baseline"
QUERYGRAVITY = "querygravity"


# ---------------------------------------------------------------------------
# Loading and aggregation
# ---------------------------------------------------------------------------
# The Brazilian-cities datasets are not UTF-8 (they carry Latin-1 accented
# names such as "São João do Polêsine-RS"), and the benchmark copies those bytes
# straight into neighbors.csv. Decoding is therefore done leniently: the city
# names are only ever echoed back, never parsed.
def open_csv(path):
    return open(path, newline="", encoding="utf-8", errors="replace")


def load_rows(path):
    with open_csv(path) as fh:
        return list(csv.DictReader(fh))


def fnum(row, key):
    """Float value of a CSV field, or None when absent/unparseable."""
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return None


def aggregate(rows):
    """Collapses the per-repetition rows into one record per config+version."""
    groups = defaultdict(list)
    for r in rows:
        key = tuple(r[k] for k in CONFIG_KEYS) + (r["version"],)
        groups[key].append(r)

    out = {}
    for key, group in groups.items():
        rec = {k: v for k, v in zip(CONFIG_KEYS + ("version",), key)}
        rec["commit_hash"] = group[0].get("commit_hash", "")
        rec["repetitions"] = len(group)
        rec["query_count"] = group[0].get("query_count", "")

        # Mean over repetitions of every numeric column.
        numeric_cols = [
            "construction_time_ms", "optimize_time_ms", "total_query_time_ms",
            "mean_query_time_ms", "median_query_time_ms", "p95_query_time_ms",
            "std_query_time_ms", "total_distance_calculations",
            "mean_distance_calculations", "total_disk_accesses",
            "mean_disk_accesses", "total_disk_writes", "index_size_bytes",
            "tree_height", "node_count", "num_objects", "mean_result_size",
            "construction_distance_calculations", "construction_disk_accesses",
        ]
        for col in numeric_cols:
            vals = [v for v in (fnum(r, col) for r in group) if v is not None]
            if not vals:
                rec[col] = ""
            elif col in ROBUST_MEDIAN_COLS:
                rec[col] = statistics.median(vals)
            else:
                rec[col] = statistics.fmean(vals)

        # Run-to-run variability of the per-repetition mean query time. This is
        # distinct from std_query_time_ms, which is the spread *within* a run.
        means = [v for v in (fnum(r, "mean_query_time_ms") for r in group) if v is not None]
        rec["between_rep_std_query_time_ms"] = (
            statistics.stdev(means) if len(means) > 1 else 0.0
        )
        # Same, for the robust timing metric that drives the verdicts.
        meds = [v for v in (fnum(r, "median_query_time_ms") for r in group)
                if v is not None]
        rec["between_rep_std_median_query_time_ms"] = (
            statistics.stdev(meds) if len(meds) > 1 else 0.0
        )
        ctimes = [v for v in (fnum(r, "construction_time_ms") for r in group) if v is not None]
        rec["between_rep_std_construction_time_ms"] = (
            statistics.stdev(ctimes) if len(ctimes) > 1 else 0.0
        )

        # Correctness: -1 means "not verified in this repetition". A config
        # counts as correct only if it was verified at least once and every
        # verified repetition passed.
        flags = [int(float(r["correct_results"])) for r in group
                 if r.get("correct_results") not in (None, "")]
        verified = [f for f in flags if f >= 0]
        rec["correct_results"] = min(verified) if verified else -1
        out[key] = rec
    return out


def sort_key(rec):
    def num(x):
        try:
            return float(x)
        except (TypeError, ValueError):
            return 0.0
    return (rec["dataset"], rec["protocol"], rec.get("query_dist", ""),
            rec["query_type"], num(rec["k"]), num(rec["radius"]))


# ---------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------
def pct_change(new, old):
    """((new - old) / old) * 100, or None when it is undefined."""
    if old in (None, "") or new in (None, ""):
        return None
    try:
        old_f, new_f = float(old), float(new)
    except (TypeError, ValueError):
        return None
    if old_f == 0:
        return None
    return ((new_f - old_f) / old_f) * 100.0


def build_comparison(agg):
    configs = sorted(
        {tuple(rec[k] for k in CONFIG_KEYS) for rec in agg.values()},
        key=lambda c: sort_key(dict(zip(CONFIG_KEYS, c))),
    )
    rows = []
    for cfg in configs:
        base = agg.get(cfg + (BASELINE,))
        qg = agg.get(cfg + (QUERYGRAVITY,))
        if not base or not qg:
            continue
        row = dict(zip(CONFIG_KEYS, cfg))
        row["baseline_commit"] = base.get("commit_hash", "")
        row["querygravity_commit"] = qg.get("commit_hash", "")
        row["repetitions"] = base["repetitions"]
        row["baseline_correct_results"] = base["correct_results"]
        row["querygravity_correct_results"] = qg["correct_results"]
        tol_time = noise_tolerance(base, qg)
        row["timing_noise_tolerance_pct"] = round(tol_time, 4)
        for metric, _label in COMPARED_METRICS:
            row[f"baseline_{metric}"] = base.get(metric, "")
            row[f"querygravity_{metric}"] = qg.get(metric, "")
            change = pct_change(qg.get(metric), base.get(metric))
            row[f"pct_change_{metric}"] = "" if change is None else round(change, 4)
            is_time = metric in TIME_METRICS
            row[f"verdict_{metric}"] = verdict(
                change, tol_time if is_time else 0.5, noisy=is_time)
        # Structural metrics, reported as absolutes rather than percentages.
        for metric in ("tree_height", "node_count", "num_objects"):
            row[f"baseline_{metric}"] = base.get(metric, "")
            row[f"querygravity_{metric}"] = qg.get(metric, "")
        rows.append(row)
    return rows


def verdict(change, tol=1.0, noisy=False):
    """Turns a percentage change into a word. Lower is better everywhere here.

    `tol` is the band inside which the change is not considered meaningful. For
    wall-clock metrics the caller passes a tolerance derived from the measured
    run-to-run variability, and `noisy` makes the inconclusive answer say so.
    """
    if change is None:
        return "n/a"
    if change < -tol:
        return "improved"
    if change > tol:
        return "worsened"
    return "within noise" if noisy else "unchanged"


def noise_tolerance(base, qg):
    """Percentage band attributable to run-to-run timing noise alone.

    Uses the larger of the two versions' relative between-repetition standard
    deviations of the mean query time, doubled (a ~2-sigma band), with a 1% floor.
    """
    worst = 0.0
    for rec in (base, qg):
        try:
            m = float(rec.get("median_query_time_ms") or 0.0)
            s = float(rec.get("between_rep_std_median_query_time_ms") or 0.0)
        except (TypeError, ValueError):
            continue
        if m > 0:
            worst = max(worst, (s / m) * 100.0)
    return max(1.0, 2.0 * worst)


# ---------------------------------------------------------------------------
# Correctness cross-check between the two versions
# ---------------------------------------------------------------------------
def compare_neighbors(results_dir):
    """Checks that both versions return the same neighbour distances."""
    paths = {
        v: os.path.join(results_dir, v, "neighbors.csv")
        for v in (BASELINE, QUERYGRAVITY)
    }
    if not all(os.path.exists(p) for p in paths.values()):
        return None

    def load(path):
        acc = defaultdict(list)
        with open_csv(path) as fh:
            for r in csv.DictReader(fh):
                key = (r["dataset"], r["protocol"], r.get("query_dist", ""),
                       r["query_type"], r["k"], r["radius"], r["query_index"])
                acc[key].append(float(r["distance"]))
        for k in acc:
            acc[k].sort()
        return acc

    a, b = load(paths[BASELINE]), load(paths[QUERYGRAVITY])
    shared = set(a) & set(b)
    mismatches = []
    for key in sorted(shared):
        da, db = a[key], b[key]
        same = len(da) == len(db) and all(
            math.isclose(x, y, rel_tol=0, abs_tol=1e-9) for x, y in zip(da, db)
        )
        if not same:
            mismatches.append(key)
    return {
        "compared": len(shared),
        "only_baseline": len(set(a) - set(b)),
        "only_querygravity": len(set(b) - set(a)),
        "mismatches": len(mismatches),
        "examples": mismatches[:5],
    }


# ---------------------------------------------------------------------------
# Minimal dependency-free SVG line chart
# ---------------------------------------------------------------------------
def svg_line_chart(path, title, subtitle, xlabel, ylabel, series, xticks,
                   xtick_fmt=None):
    """series: list of (label, colour, [(x, y), ...]).

    Every data point carries its numeric value. With two series the labels would
    collide, so the first series is annotated below its points and the second
    above; each series is also named at its right-hand end, not only in the
    legend.
    """
    W, H = 780, 460
    ML, MR, MT, MB = 92, 132, 66, 64
    pw, ph = W - ML - MR, H - MT - MB

    pts = [p for _l, _c, data in series for p in data]
    if not pts:
        return
    ys = [p[1] for p in pts]
    # Headroom at the top so the uppermost value label is not clipped.
    ymin, ymax = 0.0, max(ys) * 1.12
    if ymax == ymin:
        ymax = ymin + 1.0

    # The x axis is ORDINAL, not linear. k values like 1,5,10,20,50 placed on a
    # linear axis crowd into the left third of the plot (k=1 and k=5 land ~40px
    # apart while k=50 sits alone on the right), which collides the value labels
    # and wastes the canvas. Equal spacing per measured k reads correctly.
    xorder = sorted(set(xticks) | {p[0] for p in pts})
    xindex = {xv: i for i, xv in enumerate(xorder)}
    span = max(1, len(xorder) - 1)

    def sx(x):
        return ML + (xindex.get(x, 0) / span) * pw if len(xorder) > 1 else ML + pw / 2

    def sy(y):
        return MT + ph - (y - ymin) / (ymax - ymin) * ph

    def esc(s):
        return (str(s).replace("&", "&amp;").replace("<", "&lt;")
                .replace(">", "&gt;"))

    o = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
        f'viewBox="0 0 {W} {H}" font-family="Segoe UI,Helvetica,Arial,sans-serif">',
        f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
        f'<text x="{W/2}" y="26" text-anchor="middle" font-size="15" '
        f'font-weight="600" fill="#111">{esc(title)}</text>',
    ]
    if subtitle:
        o.append(f'<text x="{W/2}" y="46" text-anchor="middle" font-size="12" '
                 f'fill="#666">{esc(subtitle)}</text>')

    # y gridlines + labels
    for i in range(6):
        y = ymin + (ymax - ymin) * i / 5
        py = sy(y)
        o.append(f'<line x1="{ML}" y1="{py:.1f}" x2="{ML+pw}" y2="{py:.1f}" '
                 f'stroke="#e3e3e3" stroke-width="1"/>')
        lbl = f"{y:.4g}"
        o.append(f'<text x="{ML-10}" y="{py+4:.1f}" text-anchor="end" '
                 f'font-size="11" fill="#555">{esc(lbl)}</text>')

    # x ticks
    for xv in xticks:
        px = sx(xv)
        o.append(f'<line x1="{px:.1f}" y1="{MT}" x2="{px:.1f}" y2="{MT+ph}" '
                 f'stroke="#f0f0f0" stroke-width="1"/>')
        tick = xtick_fmt(xv) if xtick_fmt else f"{xv:g}"
        o.append(f'<text x="{px:.1f}" y="{MT+ph+20}" text-anchor="middle" '
                 f'font-size="11" fill="#555">{esc(tick)}</text>')

    o.append(f'<rect x="{ML}" y="{MT}" width="{pw}" height="{ph}" fill="none" '
             f'stroke="#999" stroke-width="1"/>')

    # axis labels
    o.append(f'<text x="{ML+pw/2}" y="{H-16}" text-anchor="middle" '
             f'font-size="12" fill="#333">{esc(xlabel)}</text>')
    o.append(f'<text x="18" y="{MT+ph/2}" text-anchor="middle" font-size="12" '
             f'fill="#333" transform="rotate(-90 18 {MT+ph/2})">{esc(ylabel)}</text>')

    # Which series sits lowest on screen at each x. The value label of the
    # lowest one goes below its point and the others above, so labels always
    # move apart from each other -- including where the two lines cross, which a
    # fixed per-series offset gets wrong.
    ys_at_x = {}
    for idx, (_label, _colour, data) in enumerate(series):
        for x, y in data:
            ys_at_x.setdefault(x, {})[idx] = y

    # series
    for idx, (label, colour, data) in enumerate(series):
        data = sorted(data)
        if not data:
            continue
        d = " ".join(f"{'M' if i == 0 else 'L'}{sx(x):.1f},{sy(y):.1f}"
                     for i, (x, y) in enumerate(data))
        o.append(f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="2.5"/>')

        for pos, (x, y) in enumerate(data):
            px, py = sx(x), sy(y)
            o.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="4" fill="{colour}"/>')

            others = [v for k, v in ys_at_x.get(x, {}).items() if k != idx]
            if others:
                lowest = min(others)
                below = (y < lowest) if y != lowest else (idx == 0)
            else:
                below = (idx == 0)

            # Keep the end labels inside the plot: a centred label on the first
            # or last point would spill over the y-axis numbers or the frame.
            if pos == 0:
                anchor, lx = "start", px + 5
            elif pos == len(data) - 1:
                anchor, lx = "end", px - 5
            else:
                anchor, lx = "middle", px
            o.append(f'<text x="{lx:.1f}" y="{py + (17 if below else -10):.1f}" '
                     f'text-anchor="{anchor}" font-size="10.5" font-weight="600" '
                     f'fill="{colour}">{esc(f"{y:.4g}")}</text>')

    # Name each series just past the end of its line -- clearer than a detached
    # legend. Nudge them apart when the two lines finish at similar heights.
    ends = []
    for _idx, (label, colour, data) in enumerate(series):
        d = sorted(data)
        if d:
            ends.append([sy(d[-1][1]), colour, label])
    ends.sort(key=lambda e: e[0])
    for i in range(1, len(ends)):
        if ends[i][0] - ends[i - 1][0] < 16:
            ends[i][0] = ends[i - 1][0] + 16
    for ey, colour, label in ends:
        o.append(f'<text x="{ML + pw + 12}" y="{ey + 4:.1f}" font-size="11.5" '
                 f'font-weight="600" fill="{colour}">{esc(label)}</text>')

    o.append("</svg>")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(o))


def make_charts(agg, charts_dir):
    os.makedirs(charts_dir, exist_ok=True)
    # Drop charts from an earlier run: if the metric list changed, stale files
    # would otherwise sit alongside the current ones and be mistaken for them.
    for old in glob.glob(os.path.join(charts_dir, "*.svg")):
        os.remove(old)
    made = []
    knn = [r for r in agg.values() if r["query_type"] == "knn"]
    combos = sorted({(r["dataset"], r["protocol"], r.get("query_dist", ""))
                     for r in knn})
    for dataset, protocol, qdist in combos:
        for metric, label in CHART_METRICS:
            series = []
            for version, colour in ((BASELINE, "#1f77b4"),
                                    (QUERYGRAVITY, "#d62728")):
                data = []
                for r in knn:
                    if (r["dataset"], r["protocol"], r.get("query_dist", ""),
                            r["version"]) != (dataset, protocol, qdist, version):
                        continue
                    try:
                        data.append((float(r["k"]), float(r[metric])))
                    except (TypeError, ValueError):
                        pass
                if data:
                    series.append((version, colour, data))
            if not series:
                continue

            # Subtitle: mean percentage change across the k values present in
            # both series, so the chart states the verdict without the reader
            # having to eyeball two lines.
            subtitle = ""
            if len(series) == 2:
                base = dict(series[0][2])
                qg = dict(series[1][2])
                deltas = [((qg[x] - base[x]) / base[x]) * 100.0
                          for x in sorted(set(base) & set(qg)) if base[x]]
                if deltas:
                    avg = statistics.fmean(deltas)
                    # Judge timing charts against the cell's measured noise floor.
                    is_time = metric in TIME_METRICS
                    tol = 0.5
                    if is_time:
                        cell = [r for r in knn
                                if (r["dataset"], r["protocol"],
                                    r.get("query_dist", "")) ==
                                (dataset, protocol, qdist)]
                        rel = []
                        for r in cell:
                            try:
                                m = float(r.get("median_query_time_ms") or 0)
                                s = float(r.get(
                                    "between_rep_std_median_query_time_ms") or 0)
                            except (TypeError, ValueError):
                                continue
                            if m > 0:
                                rel.append((s / m) * 100.0)
                        tol = max(1.0, 2.0 * max(rel)) if rel else 1.0
                    word = {"improved": "improvement", "worsened": "regression",
                            "unchanged": "no change",
                            "within noise": "within measurement noise",
                            "n/a": ""}[verdict(avg, tol, noisy=is_time)]
                    subtitle = (f"Query Gravity vs baseline: {avg:+.2f}% "
                                f"averaged over k ({word}; lower is better)")

            xticks = sorted({x for _l, _c, d in series for x, _y in d})
            name = f"{dataset}_{qdist}_{protocol}_{metric}.svg"
            svg_line_chart(
                os.path.join(charts_dir, name),
                f"{label} vs k  —  {dataset} / {qdist} queries / {protocol}",
                subtitle,
                "k (number of nearest neighbours)", label, series, xticks,
            )
            made.append(name)
    return made


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
def fmt(v, nd=4):
    if v in (None, ""):
        return "-"
    try:
        f = float(v)
    except (TypeError, ValueError):
        return str(v)
    if f == int(f) and abs(f) < 1e15:
        return f"{int(f)}"
    return f"{f:.{nd}g}"


def write_markdown(path, comparison, agg, neighbours, env_text, charts):
    L = []
    L.append("# Slim-Tree: baseline vs. Query Gravity\n")
    L.append("`percentage_change = ((new_value - old_value) / old_value) * 100`, "
             "with **new = Query Gravity** and **old = baseline**.\n")
    L.append("**Every metric in this report is one where lower is better, so a "
             "NEGATIVE percentage means Query Gravity IMPROVED the metric, and a "
             "POSITIVE percentage means it WORSENED it.**\n")

    L.append("## What each arm measures\n")
    L.append("| version | protocol | what it actually is |")
    L.append("|---|---|---|")
    for version, protocol, note in ARM_NOTES:
        L.append(f"| `{version}` | `{protocol}` | {note} |")
    L.append("")
    L.append("Two query distributions are exercised. `uniform` draws queries from "
             "the whole 500-city file. `skewed` concentrates them in a few hot "
             "regions: the training phase repeats a handful of points *exactly*, "
             "because `AddQueryHotspot()` only increments a hotspot's frequency "
             "when the distance is below `1e-6` — under a uniform workload the "
             "50-slot list merely churns and `CalculateHeat()` stays near zero. "
             "`skewed` is therefore the workload Query Gravity was designed for.\n")

    if env_text:
        L.append("## Execution environment\n")
        L.append("```\n" + env_text.strip() + "\n```\n")

    # ---- correctness -----------------------------------------------------
    L.append("## Correctness\n")
    bad = [r for r in comparison
           if r["baseline_correct_results"] != 1 or r["querygravity_correct_results"] != 1]
    if not comparison:
        L.append("_No comparable configurations found._\n")
    elif not bad:
        L.append("Both versions matched the exact brute-force k-NN / range search "
                 "on **every verified configuration**.\n")
    else:
        L.append("| dataset | queries | protocol | query | k | baseline correct | QG correct |")
        L.append("|---|---|---|---|---|---|---|")
        for r in bad:
            L.append(f"| {r['dataset']} | {r.get('query_dist', '')} | "
                     f"{r['protocol']} | {r['query_type']} | "
                     f"{r['k']} | {r['baseline_correct_results']} | "
                     f"{r['querygravity_correct_results']} |")
        L.append("")
        L.append("_`-1` means the configuration was not verified in that run._\n")

    if neighbours:
        L.append("### Cross-version result comparison\n")
        L.append(f"- result sets compared: **{neighbours['compared']}**")
        L.append(f"- distance mismatches between the two versions: "
                 f"**{neighbours['mismatches']}**")
        if neighbours["only_baseline"] or neighbours["only_querygravity"]:
            L.append(f"- present in only one version: "
                     f"{neighbours['only_baseline']} baseline / "
                     f"{neighbours['only_querygravity']} query gravity")
        if neighbours["examples"]:
            L.append(f"- first mismatching keys: `{neighbours['examples']}`")
        L.append("")

    # ---- per-configuration comparison ------------------------------------
    L.append("## Query performance by k\n")
    for dataset, qdist, protocol, qtype in sorted(
            {(r["dataset"], r.get("query_dist", ""), r["protocol"], r["query_type"])
             for r in comparison}):
        subset = [r for r in comparison
                  if (r["dataset"], r.get("query_dist", ""), r["protocol"],
                      r["query_type"]) == (dataset, qdist, protocol, qtype)]
        subset.sort(key=lambda r: float(r["k"]) if r["k"] else 0.0)
        L.append(f"### {dataset} — {qdist} queries — {protocol} — {qtype}\n")
        L.append("Times are median-of-medians across repetitions (robust to OS "
                 "scheduling hiccups); the counters are exact.\n")
        L.append("| k | med time base (ms) | med time QG (ms) | Δ% time | dist base | "
                 "dist QG | Δ% dist | reads base | reads QG | Δ% reads |")
        L.append("|---|---|---|---|---|---|---|---|---|---|")
        for r in subset:
            kk = r["k"] if qtype == "knn" else f"r={fmt(r['radius'])}"
            L.append(
                f"| {kk} "
                f"| {fmt(r['baseline_median_query_time_ms'])} "
                f"| {fmt(r['querygravity_median_query_time_ms'])} "
                f"| {fmt(r['pct_change_median_query_time_ms'], 3)} "
                f"| {fmt(r['baseline_mean_distance_calculations'])} "
                f"| {fmt(r['querygravity_mean_distance_calculations'])} "
                f"| {fmt(r['pct_change_mean_distance_calculations'], 3)} "
                f"| {fmt(r['baseline_mean_disk_accesses'])} "
                f"| {fmt(r['querygravity_mean_disk_accesses'])} "
                f"| {fmt(r['pct_change_mean_disk_accesses'], 3)} |"
            )
        L.append("")

    # ---- index-level metrics --------------------------------------------
    L.append("## Index construction and structure\n")
    L.append("One row per (dataset, query distribution, protocol); these do not "
             "depend on k.\n")
    L.append("| dataset | queries | protocol | build base (ms) | build QG (ms) | Δ% build | "
             "optimize base (ms) | optimize QG (ms) | height b/QG | nodes b/QG | "
             "size base | size QG |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|---|")
    seen = set()
    for r in comparison:
        key = (r["dataset"], r.get("query_dist", ""), r["protocol"])
        if key in seen:
            continue
        seen.add(key)
        L.append(
            f"| {r['dataset']} | {r.get('query_dist', '')} | {r['protocol']} "
            f"| {fmt(r['baseline_construction_time_ms'])} "
            f"| {fmt(r['querygravity_construction_time_ms'])} "
            f"| {fmt(r['pct_change_construction_time_ms'], 3)} "
            f"| {fmt(r['baseline_optimize_time_ms'])} "
            f"| {fmt(r['querygravity_optimize_time_ms'])} "
            f"| {fmt(r['baseline_tree_height'])}/{fmt(r['querygravity_tree_height'])} "
            f"| {fmt(r['baseline_node_count'])}/{fmt(r['querygravity_node_count'])} "
            f"| {fmt(r['baseline_index_size_bytes'])} "
            f"| {fmt(r['querygravity_index_size_bytes'])} |"
        )
    L.append("")

    # ---- overall verdict -------------------------------------------------
    L.append("## Verdict\n")
    for dataset, qdist, protocol in sorted(
            {(r["dataset"], r.get("query_dist", ""), r["protocol"])
             for r in comparison}):
        subset = [r for r in comparison
                  if (r["dataset"], r.get("query_dist", ""), r["protocol"])
                  == (dataset, qdist, protocol) and r["query_type"] == "knn"]
        if not subset:
            continue
        L.append(f"**{dataset} / {qdist} queries / {protocol}**\n")
        tols = [float(r.get("timing_noise_tolerance_pct") or 1.0) for r in subset]
        tol_time = statistics.fmean(tols) if tols else 1.0
        for metric, label in CHART_METRICS:
            vals = [r[f"pct_change_{metric}"] for r in subset
                    if r[f"pct_change_{metric}"] != ""]
            if not vals:
                continue
            avg = statistics.fmean(float(v) for v in vals)
            is_time = metric in TIME_METRICS
            v = verdict(avg, tol_time if is_time else 0.5, noisy=is_time)
            arrow = {"improved": "improved ✅", "worsened": "worsened ❌",
                     "unchanged": "unchanged ➖",
                     "within noise": "within measurement noise ❔", "n/a": "n/a"}[v]
            suffix = (f" (noise band ±{tol_time:.2f}%)"
                      if is_time and v == "within noise" else "")
            L.append(f"- {label}: **{avg:+.2f}%** averaged over k → {arrow}{suffix}")
        L.append("")

    if charts:
        L.append("## Charts\n")
        for c in charts:
            L.append(f"- [`charts/{c}`](charts/{c})")
        L.append("")

    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(L))


def write_csv(path, rows, preferred_first=()):
    if not rows:
        return
    cols = list(preferred_first)
    for r in rows:
        for k in r:
            if k not in cols:
                cols.append(k)
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=cols)
        w.writeheader()
        for r in rows:
            w.writerow(r)


# ---------------------------------------------------------------------------
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_results = os.path.join(os.path.dirname(here), "results")

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results", default=default_results,
                    help="results directory (default: benchmark/results)")
    args = ap.parse_args()

    combined = os.path.join(args.results, "combined_aggregated.csv")
    if not os.path.exists(combined):
        print(f"ERROR: {combined} not found. Run run_all.sh first.", file=sys.stderr)
        return 1

    rows = load_rows(combined)
    if not rows:
        print("ERROR: combined_aggregated.csv is empty.", file=sys.stderr)
        return 1

    agg = aggregate(rows)
    summary_rows = sorted(agg.values(), key=lambda r: (sort_key(r), r["version"]))
    write_csv(os.path.join(args.results, "summary_by_config.csv"), summary_rows,
              preferred_first=("version", "commit_hash") + CONFIG_KEYS)

    comparison = build_comparison(agg)
    write_csv(os.path.join(args.results, "comparison.csv"), comparison,
              preferred_first=CONFIG_KEYS)

    neighbours = compare_neighbors(args.results)
    charts = make_charts(agg, os.path.join(args.results, "charts"))

    env_path = os.path.join(args.results, "environment.txt")
    env_text = ""
    if os.path.exists(env_path):
        with open(env_path, encoding="utf-8", errors="replace") as fh:
            env_text = fh.read()

    write_markdown(os.path.join(args.results, "SUMMARY.md"),
                   comparison, agg, neighbours, env_text, charts)

    print(f"configurations aggregated : {len(summary_rows)}")
    print(f"paired comparisons        : {len(comparison)}")
    if neighbours:
        print(f"result sets cross-checked : {neighbours['compared']} "
              f"({neighbours['mismatches']} mismatches)")
    print(f"charts written            : {len(charts)}")
    print()
    print("Wrote:")
    for f in ("summary_by_config.csv", "comparison.csv", "SUMMARY.md"):
        print(f"  {os.path.join(args.results, f)}")
    print(f"  {os.path.join(args.results, 'charts')}/*.svg")
    return 0


if __name__ == "__main__":
    sys.exit(main())
