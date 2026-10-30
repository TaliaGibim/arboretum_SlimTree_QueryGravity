#!/usr/bin/env python
"""Analyses the section 4.5 experiment.

Reads the CSVs that chapter4.cpp emits and writes:

    summary.csv     one row per (regime, query_type, k, radius, arm), collapsed
                    over repetitions, with standard deviation and 95% CI
    comparison.csv  every arm against `original`, as a percentage change
    paired.csv      per-repetition paired differences between arms
    SUMMARY.md      the narrative, including the H2/H3/H4 verdicts

Every repetition runs with its own seed, so repetitions are independent
samples. Inside one repetition all arms share the workload and the insertion
order, so arms are compared PAIRED, repetition by repetition: the spread of
the paired difference is much smaller than the spread of either arm, because
the workload-to-workload variation cancels.

Stdlib only, like the Chapter 3 analyser it sits beside, so it runs wherever
the experiment does.

Why this is not benchmark/scripts/analyze.py: that one compares exactly TWO
version labels, baked into build_comparison(), ARM_NOTES and its chart code.
Chapter 4 has six arms and needs the H3 break-even and H4 decay series, which
have no analogue there. The parts that ARE arm-agnostic - the Latin-1 tolerant
reader and the collapse-over-repetitions logic - are small enough to restate
here rather than couple the two chapters together.
"""

import argparse
import csv
import io
import math
import os
import statistics
import sys

# The reference arm every other is compared against.
BASELINE = "original"

ARMS = ["original", "slimdown", "etapa1", "etapa2", "etapa2_data", "hilbert",
        "etapa2_bulk", "etapa2_bulk_data", "hilbert_etapa1", "etapa2_etapa1",
        "slimdown_etapa1"]

ARM_NOTES = {
    "original": "Slim-tree as built, in a random insertion order drawn per "
                "repetition.",
    "slimdown": "After the classic Slim-Down (Optimize()).",
    "etapa1": "Query-guided relocation, section 4.3.",
    "etapa2": "Rebuilt from the Voronoi partition of the QUERY centres, section 4.4.",
    "etapa2_data": "Control: same pipeline, same |P|, pivots from the DATA. "
                   "Isolates what the query distribution actually contributes.",
    "hilbert": "Reference: cities inserted along a Hilbert curve over "
               "(lat, long). Insertion order alone - no pivots, no queries. "
               "Uses coordinates, so it is not available in a general metric "
               "space.",
    "etapa2_bulk": "Top-down variant: T' bulk-loaded level by level, the "
                   "queries deciding the partition of every level "
                   "(stSlimQueryBulkLoad). Not height-balanced.",
    "etapa2_bulk_data": "Control for etapa2_bulk: the same code with the "
                        "queries switched off (data pivots only).",
    "hilbert_etapa1": "Etapa 1 run on the Hilbert-built tree: relocation as "
                      "fine tuning of a tree whose leaves are already tight.",
    "etapa2_etapa1": "Etapa 1 run on T' after the Etapa 2 rebuild.",
    "slimdown_etapa1": "Slim-down (Optimize()) first, then Etapa 1 on top.",
}

# (arm, reference): the comparisons that say where a gain comes from.
CONTROLS = [
    ("etapa2", "etapa2_data", "insertion order: query vs data pivots"),
    ("etapa2", "hilbert", "insertion order: query pivots vs Hilbert"),
    ("etapa2_bulk", "etapa2_bulk_data", "top-down: query vs data pivots"),
    ("etapa2_bulk", "hilbert", "top-down with queries vs Hilbert"),
    ("etapa2_bulk", "etapa2", "top-down vs insertion order, both with queries"),
    ("hilbert_etapa1", "hilbert", "Etapa 1 on top of the Hilbert tree"),
    ("etapa2_etapa1", "etapa2", "Etapa 1 on top of T'"),
    ("slimdown_etapa1", "slimdown", "Etapa 1 on top of Slim-down"),
]

CONFIG_KEYS = ("dataset", "regime", "query_type", "k", "radius")
PAIR_KEYS = ("dataset", "regime", "query_type", "k", "radius", "repetition")

# Metrics where a lower value is better, compared against the baseline arm.
COMPARED = [
    "mean_distances",
    "mean_reads",
    "mean_nodes_entered",
    "mean_subtrees_pruned",
    "median_query_time_ms",
    "mean_query_time_ms",
]

# Timings on a laptop are noisy; counters are not. Only the counters carry a
# verdict on their own.
TIME_METRICS = {"median_query_time_ms", "mean_query_time_ms", "total_query_time_ms"}

# Collapsed with a median rather than a mean, because they are already robust
# statistics and averaging them would reintroduce the outliers they excluded.
ROBUST = {"median_query_time_ms", "p95_query_time_ms"}

# Two-sided 95% Student t critical values for df = 1..30.
T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
       2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
       2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042]


def t95(df):
    if df < 1:
        return float("nan")
    if df <= 30:
        return T95[df - 1]
    if df <= 40:
        return 2.021
    if df <= 60:
        return 2.000
    if df <= 120:
        return 1.980
    return 1.960


def ci95(values):
    """Half-width of the 95% confidence interval of the mean."""
    if len(values) < 2:
        return ""
    return t95(len(values) - 1) * statistics.stdev(values) / math.sqrt(len(values))


def open_csv(path):
    """Reads a CSV that may contain Latin-1 city names."""
    if not os.path.exists(path):
        return []
    with io.open(path, encoding="latin-1", newline="") as handle:
        return list(csv.DictReader(handle))


def fnum(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def is_number(value):
    try:
        float(value)
        return True
    except (TypeError, ValueError):
        return False


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
            raw = [r.get(metric) for r in group if r.get(metric) not in (None, "")]
            if not raw:
                rec[metric] = ""
                continue
            if not all(is_number(v) for v in raw):
                rec[metric] = raw[0]  # a label, such as insertion_order
                continue
            values = [float(v) for v in raw]
            rec[metric] = statistics.median(values) if metric in ROBUST \
                else statistics.fmean(values)
            if len(values) > 1:
                rec["std_" + metric] = statistics.stdev(values)
                rec["ci95_" + metric] = ci95(values)
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
    return "%+.1f%%" % fnum(value)


def signed_ci(mean, half):
    if mean in ("", None):
        return "n/a"
    if half in ("", None):
        return signed(mean)
    return "%+.1f%% ± %.1f" % (fnum(mean), fnum(half))


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


def index_by_rep(agg):
    """(config + repetition) -> {arm: row}."""
    out = {}
    for row in agg:
        key = tuple(row.get(k, "") for k in PAIR_KEYS)
        out.setdefault(key, {})[row.get("arm", "")] = row
    return out


def paired(agg, arm, ref, metric):
    """
    Per configuration, the paired percentage change of `arm` against `ref`,
    repetition by repetition: (arm - ref) / ref * 100. Negative means `arm`
    spends less. Returns {(regime, query_type): stats}.
    """
    groups = {}
    for key, arms in index_by_rep(agg).items():
        if arm not in arms or ref not in arms:
            continue
        a = fnum(arms[arm].get(metric))
        b = fnum(arms[ref].get(metric))
        if b == 0.0:
            continue
        cfg = (arms[arm].get("regime"), arms[arm].get("query_type"))
        groups.setdefault(cfg, []).append(((a - b) / b * 100.0, a < b))

    out = {}
    for cfg, items in groups.items():
        diffs = [d for d, _ in items]
        out[cfg] = {
            "n": len(diffs),
            "mean": statistics.fmean(diffs),
            "ci95": ci95(diffs),
            "min": min(diffs),
            "max": max(diffs),
            "wins": sum(1 for _, won in items if won),
        }
    return out


def paired_rows(agg):
    """Every arm against original, and the two Etapa 2 controls, for paired.csv."""
    pairs = [(arm, BASELINE) for arm in ARMS if arm != BASELINE]
    pairs += [(arm, ref) for arm, ref, _ in CONTROLS]
    rows = []
    for arm, ref in pairs:
        for metric in ("mean_distances", "mean_reads", "mean_query_time_ms"):
            for (regime, qtype), s in sorted(paired(agg, arm, ref, metric).items()):
                rows.append({"regime": regime, "query_type": qtype, "arm": arm,
                             "reference": ref, "metric": metric,
                             "repetitions": s["n"], "mean_pct_change": s["mean"],
                             "ci95_pct": s["ci95"], "min_pct": s["min"],
                             "max_pct": s["max"], "arm_lower_in": s["wins"]})
    return rows


def amortisation(agg):
    """
    Per configuration and arm: what the reorganisation cost, what it saves per
    query against `original`, and how many queries repay it. Paired per
    repetition; the break-even is summarised with the median because one
    repetition with a near-zero saving makes it explode.
    """
    groups = {}
    for key, arms in index_by_rep(agg).items():
        base = arms.get(BASELINE)
        if base is None:
            continue
        for arm, row in arms.items():
            if arm == BASELINE:
                continue
            saving = fnum(base.get("mean_distances")) - fnum(row.get("mean_distances"))
            cost = fnum(row.get("reorg_distances"))
            cfg = (row.get("regime"), row.get("query_type"), arm)
            groups.setdefault(cfg, []).append((cost, saving))

    out = {}
    for cfg, items in groups.items():
        costs = [c for c, _ in items]
        savings = [s for _, s in items]
        breakeven = [c / s for c, s in items if s > 0]
        out[cfg] = {
            "n": len(items),
            "cost": statistics.fmean(costs),
            "saving": statistics.fmean(savings),
            "breakeven": statistics.median(breakeven) if breakeven else None,
            "never": sum(1 for s in savings if s <= 0),
        }
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
    """
    Finds the first chunk where the mean saving per measure pass exceeds the
    mean cumulative adaptation cost, averaging the repetitions chunk by chunk.
    """
    rows = [r for r in breakeven
            if r.get("regime") == regime and r.get("query_type") == qtype]
    if not rows:
        return None
    by_chunk = {}
    for row in rows:
        at = int(fnum(row.get("adapt_queries_so_far")))
        saving = fnum(row.get("baseline_total_distances")) - \
            fnum(row.get("measure_total_distances"))
        by_chunk.setdefault(at, []).append((saving, fnum(row.get("cum_adapt_distances"))))

    last = None
    for at in sorted(by_chunk):
        saving = statistics.fmean(s for s, _ in by_chunk[at])
        cost = statistics.fmean(c for _, c in by_chunk[at])
        last = (saving, cost)
        if saving > 0 and saving >= cost:
            return (at, saving, cost)
    return (None, last[0], last[1])


# ---------------------------------------------------------------------------
def config_rows(comparison):
    return sorted({(r["regime"], r["query_type"]) for r in comparison})


def metric_table(lines, comparison, agg, metric, title, note="", digits=1):
    lines.append("\n## %s\n" % title)
    if note:
        lines.append(note + "\n")
    lines.append("| regime | query | " + " | ".join(ARMS) + " |")
    lines.append("|" + "---|" * (len(ARMS) + 2))
    stats = {arm: paired(agg, arm, BASELINE, metric) for arm in ARMS[1:]}
    for regime, qtype in config_rows(comparison):
        row = next((r for r in comparison
                    if r["regime"] == regime and r["query_type"] == qtype), None)
        if row is None:
            continue
        cells = [fmt(row.get("base_" + metric), digits)]
        for arm in ARMS[1:]:
            value = row.get("%s_%s" % (arm, metric), "")
            s = stats[arm].get((regime, qtype))
            if value == "" or s is None:
                cells.append("n/a")
                continue
            cells.append("%s (%s)" % (fmt(value, digits),
                                      signed_ci(s["mean"], s["ci95"])))
        lines.append("| %s | %s | %s |" % (regime, qtype, " | ".join(cells)))


def write_markdown(path, summary, comparison, agg, moves, breakeven, struct,
                   workload):
    lines = []
    lines.append("# Chapter 4 - section 4.5 results\n")

    reps = max([int(fnum(r.get("repetitions", 1))) for r in summary] or [0])
    seeds = sorted({int(fnum(r.get("seed"))) for r in agg if is_number(r.get("seed"))})
    lines.append("%d independent repetition(s), seeds %s. Every measured query in "
                 "every arm was verified against a brute-force scan.\n"
                 % (reps, ("%d-%d" % (seeds[0], seeds[-1])) if seeds else "n/a"))

    lines.append("\n## Protocol\n")
    lines.append(
        "- Each repetition draws its own regions, queries and insertion order "
        "from its own seed (base seed + repetition).\n"
        "- A region is a metric ball: a centre city drawn at random from the "
        "data plus its nearest cities, and region balls are disjoint. Region "
        "size = factor x the queries drawn from it, so a share of every region "
        "is never queried.\n"
        "- Inside a region the cities are shuffled and split DISJOINTLY into "
        "adapt and measure queries; every region contributes to both sets.\n"
        "- `uniforme` draws adapt and measure without replacement from the "
        "query pool.\n"
        "- Percentages are PAIRED: computed per repetition against `original` "
        "on the same workload and the same insertion order, then averaged; "
        "`±` is the 95% confidence interval half-width of that mean.\n")

    lines.append("\n## Arms\n")
    for arm in ARMS:
        lines.append("- **%s** - %s" % (arm, ARM_NOTES.get(arm, "")))
    lines.append("")

    # --- workload ---------------------------------------------------------
    lines.append("\n## Workload\n")
    lines.append("`measure->adapt` is the mean distance from a measured query to "
                 "the nearest adapt query - how close the evaluation is to what "
                 "the structure was adapted to. For scale: the range radius is "
                 "%s.\n" % fmt(agg[0].get("radius"), 2))
    lines.append("| regime | regions | region size | measure->adapt (mean ± CI) |")
    lines.append("|---|---|---|---|")
    for regime in ("concentrado", "disperso", "uniforme"):
        per_rep = {}
        for r in agg:
            if r.get("regime") == regime:
                per_rep[r.get("repetition")] = (fnum(r.get("measure_to_adapt")),
                                                r.get("centres"), r.get("region_size"))
        if not per_rep:
            continue
        values = [v[0] for v in per_rep.values()]
        any_row = next(iter(per_rep.values()))
        lines.append("| %s | %s | %s | %s ± %s |" %
                     (regime, any_row[1], any_row[2] if any_row[2] != "0" else "-",
                      fmt(statistics.fmean(values), 3), fmt(ci95(values), 3)))

    if workload:
        lines.append("\nRegion centres drawn per repetition (`workload.csv` has "
                     "every query):\n")
        for regime in ("concentrado", "disperso"):
            centres = {}
            for r in workload:
                if r.get("regime") == regime and r.get("set") == "centre":
                    centres.setdefault(int(fnum(r.get("repetition"))), []).append(
                        r.get("city"))
            if not centres:
                continue
            distinct = {c for cs in centres.values() for c in cs}
            lines.append("- **%s** - %d distinct centres over %d repetitions. "
                         % (regime, len(distinct), len(centres)) +
                         "; ".join("rep %d: %s" % (rep, ", ".join(cs))
                                   for rep, cs in sorted(centres.items())[:5]) +
                         ("; ..." if len(centres) > 5 else ""))
        lines.append("")

    # --- headline tables ----------------------------------------------------
    metric_table(lines, comparison, agg, "mean_distances",
                 "Distance computations per query",
                 "Lower is better. Mean over repetitions, then the paired change "
                 "against `original` ± 95% CI.")
    metric_table(lines, comparison, agg, "mean_reads", "Page reads per query",
                 "Page reads equal nodes entered: the page manager has no buffer.")
    metric_table(lines, comparison, agg, "mean_query_time_ms",
                 "Wall-clock time per query (ms)",
                 "Supporting evidence only: tens of microseconds, on a laptop.",
                 digits=3)

    # --- what the controls say ---------------------------------------------
    lines.append("\n## Where the gain comes from\n")
    lines.append("Each row compares an arm with a control on the SAME workload and "
                 "insertion order, repetition by repetition. `etapa2` and "
                 "`etapa2_data` share pipeline and |P|; `etapa2_bulk` and "
                 "`etapa2_bulk_data` share every line of code but the query "
                 "share. Negative means the first arm computes FEWER distances; "
                 "`lower in` counts the repetitions in which it did.\n")
    lines.append("| regime | query | comparison | distances | lower in | page reads |")
    lines.append("|---|---|---|---|---|---|")
    for arm, ref, label in CONTROLS:
        dist = paired(agg, arm, ref, "mean_distances")
        reads = paired(agg, arm, ref, "mean_reads")
        for cfg in config_rows(comparison):
            a = dist.get(cfg)
            b = reads.get(cfg)
            if a is None:
                continue
            lines.append("| %s | %s | %s vs %s | %s | %d/%d | %s |" % (
                cfg[0], cfg[1], arm, ref, signed_ci(a["mean"], a["ci95"]),
                a["wins"], a["n"],
                signed_ci(b["mean"], b["ci95"]) if b else "n/a"))
    lines.append("\nAn interval that contains zero means the difference is not "
                 "distinguishable from noise at this number of repetitions.\n")

    # --- leaf depth -----------------------------------------------------------
    lines.append("\n## Leaf depth inside and outside the queried regions\n")
    lines.append("Mean depth (root = 1) of the leaf holding an object, for objects "
                 "inside a queried region (hot) and outside every one (cold). "
                 "Insertion-built trees are balanced, so both equal the height; "
                 "only the top-down arms can differ. `hot - cold` is paired per "
                 "repetition: negative means queried regions sit closer to the "
                 "root.\n")
    lines.append("| regime | arm | hot | cold | hot - cold |")
    lines.append("|---|---|---|---|---|")
    for regime in ("concentrado", "disperso"):
        for arm in ("etapa2_bulk", "etapa2_bulk_data"):
            per_rep = {}
            for r in agg:
                if (r.get("regime") == regime and r.get("arm") == arm and
                        r.get("query_type") == "knn" and
                        is_number(r.get("leaf_depth_hot")) and
                        is_number(r.get("leaf_depth_cold"))):
                    per_rep[r.get("repetition")] = (fnum(r.get("leaf_depth_hot")),
                                                    fnum(r.get("leaf_depth_cold")))
            if not per_rep:
                continue
            hot = [h for h, _ in per_rep.values()]
            cold = [c for _, c in per_rep.values()]
            diff = [h - c for h, c in per_rep.values()]
            lines.append("| %s | %s | %s | %s | %s ± %s |" % (
                regime, arm, fmt(statistics.fmean(hot), 2),
                fmt(statistics.fmean(cold), 2), fmt(statistics.fmean(diff), 2),
                fmt(ci95(diff), 2)))
    lines.append("")

    # --- amortisation -------------------------------------------------------
    lines.append("\n## Cost of reorganising, and when it pays\n")
    lines.append("`cost` is the distances spent turning the original tree into "
                 "the measured one (for etapa2: T_Q + Equation 4.9 + building T'; "
                 "for hilbert: the rebuild). `saving` is distances saved per query "
                 "against `original`. `break-even` is the median, over "
                 "repetitions, of cost / saving: the number of queries after "
                 "which the reorganisation has paid for itself.\n")
    lines.append("| regime | query | arm | cost | saving/query | break-even (queries) |")
    lines.append("|---|---|---|---|---|---|")
    am = amortisation(agg)
    for regime, qtype in config_rows(comparison):
        for arm in ARMS[1:]:
            s = am.get((regime, qtype, arm))
            if s is None:
                continue
            if s["breakeven"] is None:
                be = "never"
            else:
                be = "%.0f" % s["breakeven"]
                if s["never"]:
                    be += " (never in %d/%d)" % (s["never"], s["n"])
            lines.append("| %s | %s | %s | %s | %s | %s |" %
                         (regime, qtype, arm, fmt(s["cost"], 0),
                          fmt(s["saving"]), be))

    # --- H2 -----------------------------------------------------------------
    lines.append("\n## H2 - the destination radius never grows\n")
    growth = sum(1 for r in agg if fnum(r.get("objects_relocated")) > 0)
    lines.append("Etapa 1 relocated objects in %d measured configuration(s). The "
                 "driver aborts if the destination radius ever grows, and it did "
                 "not: every run completed. Equation 4.3 holds by construction "
                 "and was checked at run time.\n" % growth)

    # --- H3 -----------------------------------------------------------------
    lines.append("\n## H3 - amortisation of Etapa 1\n")
    if breakeven:
        lines.append("| regime | query | break-even | saving per measure pass | "
                     "cumulative adaptation cost |")
        lines.append("|---|---|---|---|---|")
        for regime, qtype in config_rows(comparison):
            verdict = h3_verdict(breakeven, regime, qtype)
            if verdict is None:
                continue
            at, saving, cost = verdict
            lines.append("| %s | %s | %s | %s | %s |" %
                         (regime, qtype,
                          ("after %d queries" % at) if at else "not reached",
                          fmt(saving), fmt(cost)))
        lines.append("\nSaving and cost are both in distance computations and are "
                     "averaged over repetitions chunk by chunk. A break-even of "
                     "*not reached* means the measured saving never grew large "
                     "enough to repay what the adaptation spent.\n")
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
        lines.append("Mean over repetitions, k-NN runs (only `etapa1` differs "
                     "between query types). The fat factor assumes every leaf "
                     "at depth H, so it is NOT comparable for the top-down arms, "
                     "which are not height-balanced.\n")
        lines.append("| regime | arm | height | index nodes | leaves | mean leaf radius "
                     "| overlap ratio | fat factor |")
        lines.append("|---|---|---|---|---|---|---|---|")
        rows = [r for r in struct if r.get("query_type", "knn") == "knn"]
        groups = {}
        for row in rows:
            groups.setdefault((row.get("regime"), row.get("arm")), []).append(row)
        for regime in ("concentrado", "disperso", "uniforme"):
            for arm in ARMS:
                group = groups.get((regime, arm))
                if not group:
                    continue
                avg = lambda m: statistics.fmean(fnum(r.get(m)) for r in group)
                lines.append("| %s | %s | %s | %s | %s | %s | %s | %s |" %
                             (regime, arm, fmt(avg("tree_height"), 1),
                              fmt(avg("index_nodes"), 0), fmt(avg("leaf_nodes"), 0),
                              fmt(avg("mean_leaf_radius"), 3),
                              fmt(avg("overlap_ratio"), 3),
                              fmt(avg("fat_factor"), 4)))

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
    workload = open_csv(os.path.join(args.results, "workload.csv"))

    metrics = [c for c in agg[0].keys()
               if c not in CONFIG_KEYS + ("commit", "arm", "repetition")]
    summary = collapse(agg, CONFIG_KEYS + ("arm",), metrics)
    comparison = build_comparison(summary)

    write_csv(os.path.join(args.results, "summary.csv"), summary)
    write_csv(os.path.join(args.results, "comparison.csv"), comparison)
    write_csv(os.path.join(args.results, "paired.csv"), paired_rows(agg))
    write_markdown(os.path.join(args.results, "SUMMARY.md"),
                   summary, comparison, agg, moves, breakeven, struct, workload)

    print("wrote summary.csv, comparison.csv, paired.csv and SUMMARY.md to %s"
          % args.results)
    return 0


if __name__ == "__main__":
    sys.exit(main())
