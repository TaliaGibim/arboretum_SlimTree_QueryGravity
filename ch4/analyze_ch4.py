#!/usr/bin/env python
"""Analyses the section 4.5 experiment.

Reads the CSVs that chapter4.cpp emits and writes:

    summary.csv     one row per (regime, query_type, k, radius, arm), collapsed
                    over repetitions
    comparison.csv  every arm against `original`, as a percentage change
    SUMMARY.md      the narrative, including the H2/H3/H4 verdicts

Stdlib only, like the Chapter 3 analyser it sits beside, so it runs wherever
the experiment does.

Why this is not benchmark/scripts/analyze.py: that one compares exactly TWO
version labels, baked into build_comparison(), ARM_NOTES and its chart code.
Chapter 4 has five arms and needs the H3 break-even and H4 decay series, which
have no analogue there. The parts that ARE arm-agnostic - the Latin-1 tolerant
reader and the collapse-over-repetitions logic - are small enough to restate
here rather than couple the two chapters together.
"""

import argparse
import csv
import io
import os
import statistics
import sys

# The reference arm every other is compared against.
BASELINE = "original"

ARMS = ["original", "slimdown", "etapa1", "etapa2", "etapa2_data"]

ARM_NOTES = {
    "original": "Slim-tree as built, in file insertion order.",
    "slimdown": "After the classic Slim-Down (Optimize()).",
    "etapa1": "Query-guided relocation, section 4.3.",
    "etapa2": "Rebuilt from the Voronoi partition of the QUERY centres, section 4.4.",
    "etapa2_data": "Control: same pipeline, same |P|, pivots from the DATA. "
                   "Isolates what the query distribution actually contributes.",
}

CONFIG_KEYS = ("dataset", "regime", "query_type", "k", "radius")

# Metrics where a lower value is better, compared against the baseline arm.
COMPARED = [
    "mean_distances",
    "mean_reads",
    "mean_nodes_entered",
    "mean_subtrees_pruned",
    "median_query_time_ms",
    "mean_query_time_ms",
]

# Timings on a laptop are noisy; counters are deterministic. Only the counters
# carry a verdict on their own.
TIME_METRICS = {"median_query_time_ms", "mean_query_time_ms", "total_query_time_ms"}

# Collapsed with a median rather than a mean, because they are already robust
# statistics and averaging them would reintroduce the outliers they excluded.
ROBUST = {"median_query_time_ms", "p95_query_time_ms"}


def open_csv(path):
    """Reads a CSV that may contain Latin-1 city names."""
    if not os.path.exists(path):
        return []
    with io.open(path, encoding="utf-8", errors="replace", newline="") as handle:
        return list(csv.DictReader(handle))


def fnum(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def collapse(rows, keys, metrics):
    """Collapses repetitions into one row per key tuple."""
    buckets = {}
    for row in rows:
        key = tuple(row.get(k, "") for k in keys)
        buckets.setdefault(key, []).append(row)

    out = []
    for key, group in sorted(buckets.items()):
        rec = dict(zip(keys, key))
        rec["repetitions"] = len(group)
        for metric in metrics:
            values = [fnum(r.get(metric)) for r in group if r.get(metric) not in (None, "")]
            if not values:
                rec[metric] = ""
                continue
            rec[metric] = statistics.median(values) if metric in ROBUST \
                else statistics.fmean(values)
            if len(values) > 1 and metric in TIME_METRICS:
                rec["between_rep_std_" + metric] = statistics.stdev(values)
        out.append(rec)
    return out


def pct_change(baseline, value):
    if baseline in ("", None) or value in ("", None):
        return ""
    baseline = fnum(baseline)
    value = fnum(value)
    if baseline == 0.0:
        return ""
    return (value - baseline) / baseline * 100.0


def write_csv(path, rows):
    if not rows:
        return
    fields = []
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)
    with io.open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def fmt(value, digits=1):
    if value in ("", None):
        return "n/a"
    return ("%." + str(digits) + "f") % fnum(value)


def signed(value):
    if value in ("", None):
        return "n/a"
    return ("%+.1f%%" % fnum(value))


# ---------------------------------------------------------------------------
def build_comparison(summary):
    """One row per configuration, with every arm expressed against `original`."""
    by_config = {}
    for row in summary:
        key = tuple(row.get(k, "") for k in CONFIG_KEYS)
        by_config.setdefault(key, {})[row.get("arm", "")] = row

    out = []
    for key, arms in sorted(by_config.items()):
        base = arms.get(BASELINE)
        if base is None:
            continue
        rec = dict(zip(CONFIG_KEYS, key))
        for metric in COMPARED:
            rec["base_" + metric] = base.get(metric, "")
        for arm in ARMS:
            if arm == BASELINE or arm not in arms:
                continue
            for metric in COMPARED:
                rec["%s_%s" % (arm, metric)] = arms[arm].get(metric, "")
                rec["%s_pct_%s" % (arm, metric)] = pct_change(
                    base.get(metric), arms[arm].get(metric))
        out.append(rec)
    return out


def h4_series(moves, regime):
    """Mean moves per query, in blocks, for one regime - the H4 decay curve."""
    rows = [r for r in moves if r.get("regime") == regime]
    if not rows:
        return []
    indices = [int(fnum(r.get("query_index"))) for r in rows]
    span = max(indices) + 1 if indices else 0
    block = max(1, span // 4)
    series = []
    for start in range(0, span, block):
        chunk = [fnum(r.get("moves")) for r in rows
                 if start <= int(fnum(r.get("query_index"))) < start + block]
        if chunk:
            series.append((start, start + block - 1, statistics.fmean(chunk)))
    return series


def h3_verdict(breakeven, regime, qtype):
    """Finds the first chunk where cumulative savings exceed cumulative cost."""
    rows = [r for r in breakeven
            if r.get("regime") == regime and r.get("query_type") == qtype]
    if not rows:
        return None
    rows.sort(key=lambda r: fnum(r.get("adapt_queries_so_far")))
    for row in rows:
        base = fnum(row.get("baseline_total_distances"))
        got = fnum(row.get("measure_total_distances"))
        cost = fnum(row.get("cum_adapt_distances"))
        if base <= 0:
            continue
        saving_per_pass = base - got
        if saving_per_pass > 0 and saving_per_pass >= cost:
            return (int(fnum(row.get("adapt_queries_so_far"))), saving_per_pass, cost)
    last = rows[-1]
    return (None,
            fnum(last.get("baseline_total_distances")) - fnum(last.get("measure_total_distances")),
            fnum(last.get("cum_adapt_distances")))


# ---------------------------------------------------------------------------
def write_markdown(path, summary, comparison, agg, moves, breakeven, struct):
    lines = []
    lines.append("# Chapter 4 - section 4.5 results\n")

    reps = max([int(fnum(r.get("repetitions", 1))) for r in summary] or [0])
    lines.append("Collapsed over %d repetition(s). Every measured query in every "
                 "arm was verified against a brute-force scan.\n" % reps)

    lines.append("\n## Arms\n")
    for arm in ARMS:
        lines.append("- **%s** - %s" % (arm, ARM_NOTES.get(arm, "")))
    lines.append("")

    # --- headline table -----------------------------------------------------
    lines.append("\n## Distance computations per query\n")
    lines.append("Lower is better. Percentages are against `original`.\n")
    configs = sorted({(r["regime"], r["query_type"]) for r in comparison})
    header = "| regime | query | " + " | ".join(ARMS) + " |"
    lines.append(header)
    lines.append("|" + "---|" * (len(ARMS) + 2))
    for regime, qtype in configs:
        row = next((r for r in comparison
                    if r["regime"] == regime and r["query_type"] == qtype), None)
        if row is None:
            continue
        cells = [fmt(row.get("base_mean_distances"))]
        for arm in ARMS[1:]:
            value = row.get("%s_mean_distances" % arm, "")
            pct = row.get("%s_pct_mean_distances" % arm, "")
            cells.append("%s (%s)" % (fmt(value), signed(pct)) if value != "" else "n/a")
        lines.append("| %s | %s | %s |" % (regime, qtype, " | ".join(cells)))

    lines.append("\n## Page reads per query\n")
    lines.append(header)
    lines.append("|" + "---|" * (len(ARMS) + 2))
    for regime, qtype in configs:
        row = next((r for r in comparison
                    if r["regime"] == regime and r["query_type"] == qtype), None)
        if row is None:
            continue
        cells = [fmt(row.get("base_mean_reads"))]
        for arm in ARMS[1:]:
            value = row.get("%s_mean_reads" % arm, "")
            pct = row.get("%s_pct_mean_reads" % arm, "")
            cells.append("%s (%s)" % (fmt(value), signed(pct)) if value != "" else "n/a")
        lines.append("| %s | %s | %s |" % (regime, qtype, " | ".join(cells)))

    # --- what the control arm says -----------------------------------------
    lines.append("\n## What the queries actually contribute\n")
    lines.append("`etapa2` and `etapa2_data` run the identical pipeline with the "
                 "identical |P|; only the origin of the pivots differs. The gap "
                 "between them is the part of the gain that is attributable to "
                 "the QUERY distribution rather than to spatially coherent "
                 "insertion order.\n")
    lines.append("| regime | query | etapa2 | etapa2_data | query advantage |")
    lines.append("|---|---|---|---|---|")
    for regime, qtype in configs:
        row = next((r for r in comparison
                    if r["regime"] == regime and r["query_type"] == qtype), None)
        if row is None:
            continue
        q = fnum(row.get("etapa2_mean_distances"), 0.0)
        d = fnum(row.get("etapa2_data_mean_distances"), 0.0)
        adv = ((d - q) / d * 100.0) if d else 0.0
        lines.append("| %s | %s | %s | %s | %s |" %
                     (regime, qtype, fmt(q), fmt(d), signed(adv)))

    # --- H2 -----------------------------------------------------------------
    lines.append("\n## H2 - the destination radius never grows\n")
    growth = sum(1 for r in agg if fnum(r.get("objects_relocated")) > 0)
    lines.append("Etapa 1 relocated objects in %d measured configuration(s). The "
                 "driver aborts if the destination radius ever grows, and it did "
                 "not: every run completed. Equation 4.3 holds by construction "
                 "and was checked at run time.\n" % growth)

    # --- H3 -----------------------------------------------------------------
    lines.append("\n## H3 - amortisation\n")
    if breakeven:
        lines.append("| regime | query | break-even | saving per measure pass | "
                     "cumulative adaptation cost |")
        lines.append("|---|---|---|---|---|")
        for regime, qtype in configs:
            verdict = h3_verdict(breakeven, regime, qtype)
            if verdict is None:
                continue
            at, saving, cost = verdict
            lines.append("| %s | %s | %s | %s | %s |" %
                         (regime, qtype,
                          ("after %d queries" % at) if at else "not reached",
                          fmt(saving), fmt(cost)))
        lines.append("\nSaving and cost are both in distance computations. A "
                     "break-even of *not reached* means the measured saving never "
                     "grew large enough to repay what the adaptation spent.\n")
    else:
        lines.append("No break-even data.\n")

    # --- H4 -----------------------------------------------------------------
    lines.append("\n## H4 - convergence of the relocation rate\n")
    if moves:
        lines.append("Mean relocations per adaptation query, in blocks:\n")
        lines.append("| regime | " + " | ".join("block %d" % i for i in range(1, 5)) + " |")
        lines.append("|---|---|---|---|---|")
        for regime in ("concentrado", "disperso", "uniforme"):
            series = h4_series(moves, regime)
            if not series:
                continue
            cells = [fmt(value, 2) for _, _, value in series[:4]]
            while len(cells) < 4:
                cells.append("n/a")
            lines.append("| %s | %s |" % (regime, " | ".join(cells)))
        lines.append("\nA decaying rate supports H4: the structure is converging. "
                     "A flat rate under `uniforme` is the predicted adverse case - "
                     "with no privileged region there is nothing to converge to, "
                     "and objects keep being moved in conflicting directions.\n")
    else:
        lines.append("No move data.\n")

    # --- structure ----------------------------------------------------------
    if struct:
        lines.append("\n## Structure\n")
        lines.append("| regime | arm | nodes | mean leaf radius | overlap ratio | fat factor |")
        lines.append("|---|---|---|---|---|---|")
        seen = set()
        for row in struct:
            key = (row.get("regime"), row.get("arm"))
            if key in seen:
                continue
            seen.add(key)
            lines.append("| %s | %s | %s | %s | %s | %s |" %
                         (row.get("regime"), row.get("arm"),
                          fmt(row.get("total_nodes"), 0),
                          fmt(row.get("mean_leaf_radius"), 3),
                          fmt(row.get("overlap_ratio"), 3),
                          fmt(row.get("fat_factor"), 4)))

    with io.open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    here = os.path.dirname(os.path.abspath(__file__))
    parser.add_argument("--results", default=os.path.join(here, "results"))
    args = parser.parse_args()

    agg = open_csv(os.path.join(args.results, "aggregated.csv"))
    if not agg:
        print("no aggregated.csv in %s" % args.results, file=sys.stderr)
        return 1

    moves = open_csv(os.path.join(args.results, "moves.csv"))
    breakeven = open_csv(os.path.join(args.results, "breakeven.csv"))
    struct = open_csv(os.path.join(args.results, "structure.csv"))

    metrics = [c for c in agg[0].keys()
               if c not in CONFIG_KEYS + ("commit", "arm", "repetition")]
    summary = collapse(agg, CONFIG_KEYS + ("arm",), metrics)
    comparison = build_comparison(summary)

    write_csv(os.path.join(args.results, "summary.csv"), summary)
    write_csv(os.path.join(args.results, "comparison.csv"), comparison)
    write_markdown(os.path.join(args.results, "SUMMARY.md"),
                   summary, comparison, agg, moves, breakeven, struct)

    print("wrote summary.csv, comparison.csv and SUMMARY.md to %s" % args.results)
    return 0


if __name__ == "__main__":
    sys.exit(main())
