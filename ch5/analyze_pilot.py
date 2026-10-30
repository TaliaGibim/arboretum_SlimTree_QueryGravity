#!/usr/bin/env python3
"""
analyze_pilot.py - Chapter 5, phase F0: summarises the qp_oracle pilot.

Reads <results>/pilot.csv (and hv.csv, environment.txt when present) and writes

  summary.csv   mean savings per (regime, query type, pivot set, S) over reps
  paired.csv    the pre-registered PAIRED comparisons, per repetition
  SUMMARY.md    tables, the comparisons and the go/no-go verdict of the plan

Standard library only.

    python analyze_pilot.py --results ch5/results_pilot
"""

import argparse
import csv
import math
import os
import statistics
import sys

REGIMES = ["concentrado", "disperso", "uniforme"]
QTYPES = ["knn", "range"]

SET_NOTES = {
    "q_medoids": "k-medoids of the adapt queries (the concentration points)",
    "q_kcenter": "farthest-first among the adapt queries, from their 1-medoid",
    "d_far": "farthest-first over the data, from the border (outlier pivots)",
    "d_medoids": "k-medoids of a random data sample (central, query-blind)",
    "d_random": "random data objects",
    "q_nearest": "ORACLE, S=1: per query, its nearest adapt query",
    "d_nearest": "control of q_nearest: nearest member of a random data "
                 "sample of the same size",
    "q_nearest_dfar": "ORACLE: nearest adapt query + (S-1) d_far pivots",
    "d_nearest_dfar": "control of q_nearest_dfar: nearest member of the "
                      "random data sample + (S-1) d_far pivots",
}
GLOBAL_Q = ["q_medoids", "q_kcenter"]
GLOBAL_D = ["d_far", "d_medoids", "d_random"]
SET_ORDER = GLOBAL_Q + GLOBAL_D + ["q_nearest", "d_nearest",
                                   "q_nearest_dfar", "d_nearest_dfar"]

# Pre-registered comparisons: (a, b, what it isolates). Positive = a saves more.
COMPARISONS = [
    ("q_medoids", "d_medoids", "same construction, queries vs data"),
    ("q_medoids", "d_far", "query concentration points vs classic outlier pivots"),
    ("q_kcenter", "d_far", "same construction (farthest-first), queries vs data"),
    ("q_nearest", "d_nearest", "local pivot: nearest past query vs nearest data sample"),
    ("q_nearest_dfar", "d_nearest_dfar", "local + global: queries vs data"),
    ("q_nearest_dfar", "d_far", "adding a local query pivot vs all-global pivots"),
]

# The go/no-go threshold of plan phase F0, in percentage points of savings.
GATE_PP = 5.0

T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
       2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
       2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042]


def t95(df):
    if df < 1:
        return float("nan")
    if df <= 30:
        return T95[df - 1]
    if df <= 60:
        return 2.000
    if df <= 120:
        return 1.980
    return 1.960


def ci95(values):
    """Half-width of the 95% confidence interval of the mean."""
    if len(values) < 2:
        return float("nan")
    return t95(len(values) - 1) * statistics.stdev(values) / math.sqrt(len(values))


def fmt(x, digits=1):
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return "-"
    return f"{x:.{digits}f}"


def pm(mean, half, digits=1):
    if math.isnan(half):
        return fmt(mean, digits)
    return f"{fmt(mean, digits)} ± {fmt(half, digits)}"


def signed_pm(mean, half, digits=1):
    s = f"{mean:+.{digits}f}"
    return s if math.isnan(half) else f"{s} ± {half:.{digits}f}"


def load(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--results", required=True)
    args = ap.parse_args()
    res = args.results

    pilot_path = os.path.join(res, "pilot.csv")
    if not os.path.exists(pilot_path):
        sys.exit(f"no {pilot_path}")
    rows = load(pilot_path)

    # index: (regime, qtype, set, slots) -> {rep: row}
    by = {}
    for r in rows:
        key = (r["regime"], r["query_type"], r["set"], int(r["slots"]))
        by.setdefault(key, {})[int(r["repetition"])] = r
    reps = sorted({int(r["repetition"]) for r in rows})
    regimes = [g for g in REGIMES if any(k[0] == g for k in by)]
    qtypes = [q for q in QTYPES if any(k[1] == q for k in by)]
    sets = [s for s in SET_ORDER if any(k[2] == s for k in by)]
    slots_max = max(k[3] for k in by)

    all_exact = all(r["replica_exact"] == "1" for r in rows)
    all_correct = all(r["correct"] == "1" for r in rows)

    def series(regime, qtype, s, S, col):
        cell = by.get((regime, qtype, s, S), {})
        return {rep: float(row[col]) for rep, row in cell.items()}

    # ---- summary.csv -------------------------------------------------------
    summary = []
    for g in regimes:
        for q in qtypes:
            for s in ["none"] + sets:
                for S in range(0, slots_max + 1):
                    if (g, q, s, S) not in by:
                        continue
                    sav = list(series(g, q, s, S, "saving_distances_pct").values())
                    savr = list(series(g, q, s, S, "saving_reads_pct").values())
                    dist = list(series(g, q, s, S, "distances").values())
                    reads = list(series(g, q, s, S, "reads").values())
                    qtp = [float(r["query_to_pivot"]) for r in by[(g, q, s, S)].values()
                           if r["query_to_pivot"] != ""]
                    summary.append({
                        "regime": g, "query_type": q, "set": s, "slots": S,
                        "reps": len(sav),
                        "distances": statistics.mean(dist),
                        "reads": statistics.mean(reads),
                        "saving_distances_pct": statistics.mean(sav),
                        "saving_distances_ci": ci95(sav),
                        "saving_reads_pct": statistics.mean(savr),
                        "saving_reads_ci": ci95(savr),
                        "query_to_pivot": statistics.mean(qtp) if qtp else "",
                    })
    with open(os.path.join(res, "summary.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(summary[0].keys()))
        w.writeheader()
        w.writerows(summary)
    summ = {(x["regime"], x["query_type"], x["set"], x["slots"]): x for x in summary}

    # ---- paired.csv --------------------------------------------------------
    def paired(regime, qtype, a, b, S, col="saving_distances_pct"):
        sa = series(regime, qtype, a, S, col)
        sb = series(regime, qtype, b, S, col)
        common = sorted(set(sa) & set(sb))
        diffs = [sa[r] - sb[r] for r in common]
        if not diffs:
            return None
        return {"mean": statistics.mean(diffs), "ci": ci95(diffs),
                "wins": sum(1 for d in diffs if d > 0), "n": len(diffs)}

    paired_rows = []
    for a, b, what in COMPARISONS:
        for g in regimes:
            for q in qtypes:
                for S in range(1, slots_max + 1):
                    p = paired(g, q, a, b, S)
                    if p is None:
                        continue
                    pr = paired(g, q, a, b, S, "saving_reads_pct")
                    paired_rows.append({
                        "a": a, "b": b, "regime": g, "query_type": q, "slots": S,
                        "mean_pp": p["mean"], "ci_pp": p["ci"], "wins": p["wins"],
                        "reads_mean_pp": pr["mean"], "reads_ci_pp": pr["ci"],
                        "reads_wins": pr["wins"], "reps": p["n"], "isolates": what})
    if paired_rows:
        with open(os.path.join(res, "paired.csv"), "w", newline="",
                  encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(paired_rows[0].keys()))
            w.writeheader()
            w.writerows(paired_rows)

    # ---- the verdict -------------------------------------------------------
    # For every (regime, qtype, S): the best query-derived set against the best
    # data-derived set, both chosen by their mean, then compared PAIRED. Choosing
    # the best data set after the fact favours the data side: conservative.
    def best_of(candidates, g, q, S):
        present = [c for c in candidates if (g, q, c, S) in summ]
        if not present:
            return None
        return max(present, key=lambda c: summ[(g, q, c, S)]["saving_distances_pct"])

    verdict_rows = []
    for family, qset, dset in [
            ("global", GLOBAL_Q, GLOBAL_D),
            ("local (oracle)", ["q_nearest", "q_nearest_dfar"],
             ["d_nearest", "d_nearest_dfar"])]:
        for g in regimes:
            for q in qtypes:
                for S in range(1, slots_max + 1):
                    a, b = best_of(qset, g, q, S), best_of(dset, g, q, S)
                    if a is None or b is None:
                        continue
                    p = paired(g, q, a, b, S)
                    if p is None:
                        continue
                    lower = p["mean"] - (0 if math.isnan(p["ci"]) else p["ci"])
                    verdict_rows.append({
                        "family": family, "regime": g, "query_type": q, "slots": S,
                        "best_q": a, "best_d": b, "mean": p["mean"], "ci": p["ci"],
                        "wins": p["wins"], "n": p["n"],
                        "passes": p["mean"] >= GATE_PP and lower > 0})

    def family_passes(family):
        """Passes when, for concentrado AND disperso, some S clears the gate in
        every query type measured."""
        ok = True
        for g in ("concentrado", "disperso"):
            if g not in regimes:
                return None
            for q in qtypes:
                ok = ok and any(v["passes"] for v in verdict_rows
                                if v["family"] == family and v["regime"] == g
                                and v["query_type"] == q)
        return ok

    # ---- SUMMARY.md --------------------------------------------------------
    hv = load(os.path.join(res, "hv.csv")) if os.path.exists(
        os.path.join(res, "hv.csv")) else []
    env = ""
    if os.path.exists(os.path.join(res, "environment.txt")):
        with open(os.path.join(res, "environment.txt"), encoding="utf-8") as f:
            env = f.read()

    L = []
    L.append("# Chapter 5, phase F0 - pivot pilot (qp_oracle)\n")
    L.append(f"{len(reps)} repetition(s), {', '.join(regimes)}; S = 1..{slots_max} "
             "pivots per set. Same seeds, regions, queries, insertion order and "
             "tree as the Chapter 4 `original` arm.\n")
    L.append(f"- Replica of the Slim-tree search reproduces the real tree's "
             f"distance and page counts on every query: **{'yes' if all_exact else 'NO'}**")
    L.append(f"- Every answer, with and without pivots, matches brute force: "
             f"**{'yes' if all_correct else 'NO'}**\n")
    L.append("Savings are PAIRED per repetition against the same tree without "
             "pivots, and include the cost of the pivots (S distances d(q,p) per "
             "query). `±` is the 95% CI half-width over repetitions. The space "
             "pivot values would take in a page is NOT modelled: this measures "
             "pruning on the same tree.\n")

    L.append("## What each set is\n")
    for s in sets:
        L.append(f"- **{s}** - {SET_NOTES.get(s, '')}")
    L.append("")

    if hv:
        h = hv[0]
        L.append("## Homogeneity of viewpoints (Ciaccia, Patella & Zezula, PODS'98)\n")
        L.append(f"HV = **{float(h['hv']):.3f}** over {h['sample']} objects "
                 f"(mean discrepancy {float(h['delta_mean']):.3f}; p10/p50/p90 = "
                 f"{float(h['delta_p10']):.3f} / {float(h['delta_p50']):.3f} / "
                 f"{float(h['delta_p90']):.3f}; d+ = {float(h['d_plus']):.1f}). "
                 "Their real and synthetic data sets all had HV > 0.98: the lower "
                 "the HV, the more objects 'see' the data differently, and the "
                 "more the position of a pivot can matter.\n")

    L.append("## Baseline (no pivots)\n")
    L.append("| regime | k-NN distances | k-NN reads | range distances | range reads |")
    L.append("|---|---|---|---|---|")
    for g in regimes:
        cells = []
        for q in ["knn", "range"]:
            x = summ.get((g, q, "none", 0))
            cells += [fmt(x["distances"]) if x else "-", fmt(x["reads"]) if x else "-"]
        L.append(f"| {g} | " + " | ".join(cells) + " |")
    L.append("")

    for metric, ci, title in [
            ("saving_distances_pct", "saving_distances_ci",
             "Distance savings (%), pivot cost included"),
            ("saving_reads_pct", "saving_reads_ci", "Page-read savings (%)")]:
        L.append(f"## {title}\n")
        header = "| set | S | " + " | ".join(f"{g} {q}" for g in regimes for q in qtypes) + " |"
        L.append(header)
        L.append("|---|---|" + "---|" * (len(regimes) * len(qtypes)))
        for s in sets:
            for S in range(1, slots_max + 1):
                if not any((g, q, s, S) in summ for g in regimes for q in qtypes):
                    continue
                cells = []
                for g in regimes:
                    for q in qtypes:
                        x = summ.get((g, q, s, S))
                        cells.append(pm(x[metric], x[ci]) if x else "-")
                L.append(f"| {s} | {S} | " + " | ".join(cells) + " |")
        L.append("")

    L.append("## Paired comparisons (percentage points of savings)\n")
    L.append("Positive = the first set saves more. `wins` = repetitions where it "
             "did. The go/no-go below uses distances only, as fixed in the plan; "
             "page reads are reported for information.\n")
    for a, b, what in COMPARISONS:
        sub = [p for p in paired_rows if p["a"] == a and p["b"] == b]
        if not sub:
            continue
        L.append(f"### {a} vs {b} - {what}\n")
        for col, ci, wins, label in [("mean_pp", "ci_pp", "wins", "distances"),
                                     ("reads_mean_pp", "reads_ci_pp", "reads_wins",
                                      "page reads")]:
            L.append(f"{label}:\n")
            L.append("| S | " + " | ".join(f"{g} {q}" for g in regimes for q in qtypes) + " |")
            L.append("|---|" + "---|" * (len(regimes) * len(qtypes)))
            for S in sorted({p["slots"] for p in sub}):
                cells = []
                for g in regimes:
                    for q in qtypes:
                        m = [p for p in sub if p["regime"] == g and p["query_type"] == q
                             and p["slots"] == S]
                        cells.append(f"{signed_pm(m[0][col], m[0][ci])} "
                                     f"({m[0][wins]}/{m[0]['reps']})" if m else "-")
                L.append(f"| {S} | " + " | ".join(cells) + " |")
            L.append("")

    L.append("## Go/no-go (plan, phase F0)\n")
    L.append(f"Criterion fixed before the run: the best query-derived set beats the "
             f"best data-derived set by at least **{GATE_PP:.0f} percentage points** "
             "of distance savings, with the paired 95% CI above zero, in "
             "`concentrado` AND `disperso`, for some S, in every query type. Both "
             "best sets are picked by their mean, which favours the data side.\n")
    for family in ["global", "local (oracle)"]:
        sub = [v for v in verdict_rows if v["family"] == family]
        if not sub:
            continue
        verdict = family_passes(family)
        word = {True: "PASSES", False: "DOES NOT PASS", None: "not decidable"}[verdict]
        L.append(f"### {family} pivots: **{word}**\n")
        L.append("| regime | query | S | best query set | best data set | "
                 "difference (pp) | wins | clears 5 pp |")
        L.append("|---|---|---|---|---|---|---|---|")
        for v in sub:
            L.append(f"| {v['regime']} | {v['query_type']} | {v['slots']} | "
                     f"{v['best_q']} | {v['best_d']} | {signed_pm(v['mean'], v['ci'])} "
                     f"| {v['wins']}/{v['n']} | {'yes' if v['passes'] else 'no'} |")
        L.append("")

    if env:
        L.append("## Environment\n")
        L.append("```")
        L.append(env.rstrip())
        L.append("```")

    with open(os.path.join(res, "SUMMARY.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")

    print(f"wrote {os.path.join(res, 'SUMMARY.md')}")
    for family in ["global", "local (oracle)"]:
        verdict = family_passes(family)
        if verdict is not None:
            print(f"  gate, {family} pivots: {'PASSES' if verdict else 'does not pass'}")
    if not (all_exact and all_correct):
        print("  WARNING: replica or verification failures - see SUMMARY.md")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
