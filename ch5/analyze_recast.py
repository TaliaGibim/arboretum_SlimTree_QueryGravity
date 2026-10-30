#!/usr/bin/env python3
"""
analyze_recast.py - Chapter 5, phase R0: summarises the qp_recast runs.

Reads <results>/recast.csv and online.csv (and environment.txt when present)
and writes

  summary.csv   means over repetitions per (regime, order, query type, arm,
                phase, K)
  paired.csv    the paired comparisons, per repetition
  SUMMARY.md    tables, the comparisons and the verdict on each hypothesis

Standard library only.

    python analyze_recast.py --results ch5/results_recast
"""

import argparse
import csv
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_pilot import ci95, fmt, pm, signed_pm, load  # noqa: E402

REGIMES = ["concentrado", "disperso", "uniforme"]
QTYPES = ["knn", "range"]
ORDERS = ["mixed", "salto"]

# Threshold of H-R0.2, the same as the F0 gate, in percentage points.
GATE_PP = 5.0

ARM_NOTES = {
    "none": "the Slim-tree as it is (replica, checked against the real tree)",
    "recast": "paid columns left by the adapt queries in the nodes they read; "
              "frozen for the measured queries",
    "recast_live": "the same tables, still learning during the measured queries",
    "recast_data": "CONTROL: the same mechanism, columns left by as many "
                   "'queries' on random cities (their cost is overhead)",
    "oracle_q": "F0 ceiling: full column of the nearest adapt query, found for free",
    "oracle_d": "control of oracle_q: nearest member of a random data sample",
    "pm_dfar4": "reference: 4 global data pivots with full columns (PM-tree "
                "style; installation and page space not charged)",
}


def f(row, key):
    v = row.get(key, "")
    return float(v) if v not in ("", None) else float("nan")


def mean(xs):
    xs = [x for x in xs if not math.isnan(x)]
    return statistics.fmean(xs) if xs else float("nan")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--results", required=True)
    args = ap.parse_args()
    res = args.results

    path = os.path.join(res, "recast.csv")
    if not os.path.exists(path):
        sys.exit(f"no {path}")
    rows = load(path)
    online_path = os.path.join(res, "online.csv")
    online = load(online_path) if os.path.exists(online_path) else []

    # (regime, order, qtype, arm, phase, K) -> {rep: row}
    by = {}
    for r in rows:
        key = (r["regime"], r["order"], r["query_type"], r["arm"], r["phase"],
               int(r["node_slots"]))
        by.setdefault(key, {})[int(r["repetition"])] = r
    reps = sorted({int(r["repetition"]) for r in rows})
    Ks = sorted({int(r["node_slots"]) for r in rows if r["arm"] == "recast"})
    regimes = [g for g in REGIMES if any(k[0] == g for k in by)]
    orders = [o for o in ORDERS if any(k[1] == o for k in by)]
    qtypes = [q for q in QTYPES if any(k[2] == q for k in by)]

    all_correct = all(r["correct"] == "1" for r in rows)
    replica_exact = all(r["replica_exact"] == "1" for r in rows)
    engine_exact = all(r["engine_exact"] == "1" for r in rows)

    # ---- summary.csv ------------------------------------------------------
    metrics = ["base_distances", "base_reads", "distances", "index_distances",
               "leaf_distances", "pivot_distances", "reads",
               "saving_distances_pct", "saving_reads_pct", "pruned_subtrees",
               "pruned_objects", "dropped_at_pop", "pivots_registered",
               "slots_stored", "intervals_stored", "confirmed_slots",
               "admitted", "evicted", "refused"]
    summary = {}
    with open(os.path.join(res, "summary.csv"), "w", newline="",
              encoding="utf-8") as out:
        w = csv.writer(out)
        w.writerow(["regime", "order", "query_type", "arm", "phase", "node_slots",
                    "reps"] + metrics + ["saving_distances_ci", "saving_reads_ci"])
        for key in sorted(by):
            per = by[key]
            vals = {m: [f(per[r], m) for r in sorted(per)] for m in metrics}
            means = {m: mean(vals[m]) for m in metrics}
            summary[key] = dict(means)
            summary[key]["dist_ci"] = ci95(vals["saving_distances_pct"])
            summary[key]["reads_ci"] = ci95(vals["saving_reads_pct"])
            summary[key]["dist_all"] = vals["saving_distances_pct"]
            summary[key]["reads_all"] = vals["saving_reads_pct"]
            w.writerow(list(key) + [len(per)] + [means[m] for m in metrics] +
                       [summary[key]["dist_ci"], summary[key]["reads_ci"]])

    # ---- paired comparisons -------------------------------------------------
    def paired(regime, order, qtype, a, b):
        """a and b: (arm, phase, K). Positive = a saves more than b."""
        ka = (regime, order, qtype) + a
        kb = (regime, order, qtype) + b
        if ka not in by or kb not in by:
            return None
        common = sorted(set(by[ka]) & set(by[kb]))
        if not common:
            return None
        dd = [f(by[ka][r], "saving_distances_pct") - f(by[kb][r], "saving_distances_pct")
              for r in common]
        dr = [f(by[ka][r], "saving_reads_pct") - f(by[kb][r], "saving_reads_pct")
              for r in common]
        return {"reps": common, "dist": dd, "reads": dr,
                "dist_mean": mean(dd), "dist_ci": ci95(dd),
                "reads_mean": mean(dr), "reads_ci": ci95(dr),
                "dist_pos": sum(1 for x in dd if x > 0),
                "reads_pos": sum(1 for x in dr if x > 0)}

    comparisons = []
    for order in orders:
        for g in regimes:
            for q in qtypes:
                for K in Ks:
                    for (a, b, what) in [
                        (("recast", "measure", K), ("recast_data", "measure", K),
                         "queries vs random cities (same mechanism, same K)"),
                        (("recast", "measure", K), ("none", "measure", 0),
                         "recast vs the Slim-tree"),
                        (("recast", "adapt", K), ("none", "adapt", 0),
                         "online, while the tables are being built"),
                        (("recast_live", "measure", K), ("recast", "measure", K),
                         "still learning vs frozen"),
                        (("oracle_q", "measure", 1), ("recast", "measure", K),
                         "how far below the F0 ceiling"),
                        (("pm_dfar4", "measure", 4), ("recast", "measure", K),
                         "4 global data pivots vs recast"),
                    ]:
                        p = paired(g, order, q, a, b)
                        if p:
                            comparisons.append((order, g, q, K, a, b, what, p))

    with open(os.path.join(res, "paired.csv"), "w", newline="",
              encoding="utf-8") as out:
        w = csv.writer(out)
        w.writerow(["order", "regime", "query_type", "node_slots", "a", "b",
                    "what", "repetition", "diff_distances_pp", "diff_reads_pp"])
        for (order, g, q, K, a, b, what, p) in comparisons:
            for i, r in enumerate(p["reps"]):
                w.writerow([order, g, q, K, "/".join(map(str, a)),
                            "/".join(map(str, b)), what, r,
                            p["dist"][i], p["reads"][i]])

    def cmp(order, g, q, K, a_arm, b_arm, a_phase="measure", b_phase="measure",
            bK=None):
        for (o, gg, qq, KK, a, b, what, p) in comparisons:
            if (o, gg, qq, KK) == (order, g, q, K) and a[0] == a_arm and \
                    b[0] == b_arm and a[1] == a_phase and b[1] == b_phase and \
                    (bK is None or b[2] == bK):
                return p
        return None

    # ---- SUMMARY.md ---------------------------------------------------------
    L = []
    L.append("# Chapter 5, phase R0 - RECAST-style paid pivot columns in the Slim-tree")
    L.append("")
    env = os.path.join(res, "environment.txt")
    if os.path.exists(env):
        L.append("```")
        with open(env, encoding="utf-8") as fh:
            L.extend(line.rstrip("\n") for line in fh)
        L.append("```")
        L.append("")
    L.append(f"Repetitions: {len(reps)}. Node slots K: {', '.join(map(str, Ks))}.")
    L.append(f"All answers exact (brute force): **{'yes' if all_correct else 'NO'}**. "
             f"Replica = real tree: **{'yes' if replica_exact else 'NO'}**. "
             f"Engine with empty tables = replica: "
             f"**{'yes' if engine_exact else 'NO'}**.")
    L.append("")
    L.append("Savings are in % of the Slim-tree (`none`) on the same queries, mean "
             "± 95% CI over repetitions. Distances include every d(q,p) paid to a "
             "pivot. Differences between arms are paired by repetition, in "
             "percentage points (pp).")
    L.append("")
    L.append("| arm | what |")
    L.append("|---|---|")
    for arm, note in ARM_NOTES.items():
        L.append(f"| `{arm}` | {note} |")
    L.append("")

    def s(key, field):
        return summary.get(key, {}).get(field, float("nan"))

    for order in orders:
        title = "mixed stream (Chapter 4 protocol)" if order == "mixed" else \
            "salto: adapt queries region by region (the focus jumps)"
        L.append(f"## Measured queries - {title}")
        L.append("")
        for q in qtypes:
            L.append(f"### {q}")
            L.append("")
            hdr = "| regime | Slim-tree dist / reads |"
            sep = "|---|---|"
            for K in Ks:
                hdr += f" recast K={K}: dist % / reads % (d(q,p)) |"
                sep += "---|"
            hdr += " recast_data K=" + str(Ks[-1]) + " | oracle_q | pm_dfar4 |"
            sep += "---|---|---|"
            L.append(hdr)
            L.append(sep)
            for g in regimes:
                kn = (g, order, q, "none", "measure", 0)
                if kn not in summary:
                    continue
                line = f"| {g} | {fmt(s(kn, 'distances'))} / {fmt(s(kn, 'reads'))} |"
                for K in Ks:
                    k = (g, order, q, "recast", "measure", K)
                    line += (f" {pm(s(k, 'saving_distances_pct'), s(k, 'dist_ci'))} / "
                             f"{pm(s(k, 'saving_reads_pct'), s(k, 'reads_ci'))} "
                             f"({fmt(s(k, 'pivot_distances'))}) |")
                k = (g, order, q, "recast_data", "measure", Ks[-1])
                line += (f" {fmt(s(k, 'saving_distances_pct'))} / "
                         f"{fmt(s(k, 'saving_reads_pct'))} |")
                k = (g, order, q, "oracle_q", "measure", 1)
                line += (f" {fmt(s(k, 'saving_distances_pct'))} / "
                         f"{fmt(s(k, 'saving_reads_pct'))} |")
                k = (g, order, q, "pm_dfar4", "measure", 4)
                line += (f" {fmt(s(k, 'saving_distances_pct'))} / "
                         f"{fmt(s(k, 'saving_reads_pct'))} |")
                L.append(line)
            L.append("")

        L.append(f"### Queries vs random cities, same mechanism ({order})")
        L.append("")
        L.append("`recast` minus `recast_data`, measured queries. Positive = the "
                 "columns left by the queries prune more. n+ = repetitions where "
                 "`recast` wins.")
        L.append("")
        L.append("| regime | query | K | distances (pp) | n+ | reads (pp) | n+ |")
        L.append("|---|---|---|---|---|---|---|")
        for g in regimes:
            for q in qtypes:
                for K in Ks:
                    p = cmp(order, g, q, K, "recast", "recast_data")
                    if not p:
                        continue
                    L.append(f"| {g} | {q} | {K} | "
                             f"{signed_pm(p['dist_mean'], p['dist_ci'])} | "
                             f"{p['dist_pos']}/{len(p['reps'])} | "
                             f"{signed_pm(p['reads_mean'], p['reads_ci'])} | "
                             f"{p['reads_pos']}/{len(p['reps'])} |")
        L.append("")

        L.append(f"### Online, while the adapt queries build the tables ({order})")
        L.append("")
        L.append("| regime | query | K | distances % | reads % | d(q,p) per query |")
        L.append("|---|---|---|---|---|---|")
        for g in regimes:
            for q in qtypes:
                for K in Ks:
                    k = (g, order, q, "recast", "adapt", K)
                    if k not in summary:
                        continue
                    L.append(f"| {g} | {q} | {K} | "
                             f"{pm(s(k, 'saving_distances_pct'), s(k, 'dist_ci'))} | "
                             f"{pm(s(k, 'saving_reads_pct'), s(k, 'reads_ci'))} | "
                             f"{fmt(s(k, 'pivot_distances'))} |")
        L.append("")

        L.append(f"### Still learning vs frozen, and share of the F0 ceiling ({order})")
        L.append("")
        L.append("| regime | query | K | live - frozen, distances (pp) | "
                 "recast / oracle_q savings |")
        L.append("|---|---|---|---|---|")
        for g in regimes:
            for q in qtypes:
                for K in Ks:
                    p = cmp(order, g, q, K, "recast_live", "recast")
                    kr = (g, order, q, "recast", "measure", K)
                    ko = (g, order, q, "oracle_q", "measure", 1)
                    share = s(kr, "saving_distances_pct") / s(ko, "saving_distances_pct") \
                        if s(ko, "saving_distances_pct") else float("nan")
                    if not p:
                        continue
                    L.append(f"| {g} | {q} | {K} | "
                             f"{signed_pm(p['dist_mean'], p['dist_ci'])} | "
                             f"{fmt(100 * share, 0)}% |")
        L.append("")

    # ---- online curve under salto ----------------------------------------
    if online and "salto" in orders:
        L.append("## The focus jumps: savings per block of adapt queries (salto)")
        L.append("")
        L.append("Mean over repetitions of the distance saving of each block of "
                 "consecutive adapt queries; `region` is the region the block "
                 "queries. Every jump starts in nodes no earlier query has "
                 "read, so the first block of a region starts from (almost) "
                 "empty tables.")
        L.append("")
        curve = {}
        for r in online:
            if r["order"] != "salto":
                continue
            key = (r["regime"], r["query_type"], int(r["node_slots"]), int(r["block"]))
            base = f(r, "base_distances")
            sav = 100.0 * (base - f(r, "distances")) / base if base else float("nan")
            curve.setdefault(key, {"sav": [], "region": r["region"]})["sav"].append(sav)
        for g in [x for x in regimes if x != "uniforme"]:
            for q in qtypes:
                K = Ks[-1]
                blocks = sorted(b for (gg, qq, KK, b) in curve if (gg, qq, KK) == (g, q, K))
                if not blocks:
                    continue
                L.append(f"**{g}, {q}, K={K}**")
                L.append("")
                L.append("| block | " + " | ".join(str(b) for b in blocks) + " |")
                L.append("|---|" + "---|" * len(blocks))
                L.append("| region | " + " | ".join(
                    curve[(g, q, K, b)]["region"] for b in blocks) + " |")
                L.append("| saving % | " + " | ".join(
                    fmt(mean(curve[(g, q, K, b)]["sav"])) for b in blocks) + " |")
                L.append("")

    # ---- memory -------------------------------------------------------------
    L.append("## What the tables hold at the end of the adapt phase (mixed)")
    L.append("")
    L.append("| regime | query | K | pivots (queries kept) | slots | intervals | "
             "confirmed | admitted | evicted | refused |")
    L.append("|---|---|---|---|---|---|---|---|---|---|")
    for g in regimes:
        for q in qtypes:
            for K in Ks:
                k = (g, "mixed", q, "recast", "adapt", K)
                if k not in summary:
                    continue
                L.append(f"| {g} | {q} | {K} | {fmt(s(k, 'pivots_registered'), 0)} | "
                         f"{fmt(s(k, 'slots_stored'), 0)} | "
                         f"{fmt(s(k, 'intervals_stored'), 0)} | "
                         f"{fmt(s(k, 'confirmed_slots'), 0)} | "
                         f"{fmt(s(k, 'admitted'), 0)} | {fmt(s(k, 'evicted'), 0)} | "
                         f"{fmt(s(k, 'refused'), 0)} |")
    L.append("")

    # ---- hypotheses ---------------------------------------------------------
    L.append("## Hypotheses (fixed before the 20-repetition run)")
    L.append("")

    # H-R0.1: recast < none, every regime, both query types, some K.
    h1 = []
    for g in regimes:
        for q in qtypes:
            best = None
            for K in Ks:
                k = (g, "mixed", q, "recast", "measure", K)
                if k in summary and summary[k]["saving_distances_pct"] - \
                        summary[k]["dist_ci"] > 0:
                    best = K
                    break
            h1.append((g, q, best))
    ok1 = all(b is not None for (_, _, b) in h1)
    L.append(f"- **H-R0.1** `recast` costs fewer distances than the Slim-tree "
             f"(CI above 0), in every regime and query type, for some K: "
             f"**{'holds' if ok1 else 'does not hold'}**.")

    # H-R0.2: recast - recast_data >= GATE_PP with CI > 0 in concentrado AND
    # disperso, both query types, for some K (distances).
    h2 = {}
    for g in ["concentrado", "disperso"]:
        for q in qtypes:
            hit = []
            for K in Ks:
                p = cmp("mixed", g, q, K, "recast", "recast_data")
                if p and p["dist_mean"] >= GATE_PP and p["dist_mean"] - p["dist_ci"] > 0:
                    hit.append(K)
            h2[(g, q)] = hit
    ok2 = all(h2.get((g, q)) for g in ["concentrado", "disperso"] for q in qtypes
              if g in regimes)
    L.append(f"- **H-R0.2** the columns of the queries beat the columns of random "
             f"cities by at least {GATE_PP:.0f} pp of distances (paired CI above 0) "
             f"in concentrado AND disperso, both query types, for some K: "
             f"**{'holds' if ok2 else 'does not hold'}**.")
    for (g, q), hit in h2.items():
        L.append(f"  - {g}, {q}: K = {', '.join(map(str, hit)) if hit else 'none'}")
    # H-R0.3: uniforme, no difference expected.
    u = []
    for q in qtypes:
        for K in Ks:
            p = cmp("mixed", "uniforme", q, K, "recast", "recast_data")
            if p:
                u.append(f"{q} K={K}: {signed_pm(p['dist_mean'], p['dist_ci'])} pp")
    L.append(f"- **H-R0.3** in uniforme the two are within noise "
             f"(descriptive): {'; '.join(u) if u else '-'}.")
    # H-R0.4: reads.
    rr = []
    for g in regimes:
        for q in qtypes:
            k = (g, "mixed", q, "recast", "measure", Ks[-1])
            if k in summary:
                rr.append(f"{g}/{q} {fmt(s(k, 'saving_reads_pct'))}%")
    L.append(f"- **H-R0.4** (descriptive) page reads saved by `recast`, K={Ks[-1]}: "
             f"{'; '.join(rr)}.")
    L.append("")

    with open(os.path.join(res, "SUMMARY.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(L) + "\n")

    print(f"answers exact: {all_correct}; replica exact: {replica_exact}; "
          f"empty engine exact: {engine_exact}")
    print(f"H-R0.1 {'holds' if ok1 else 'does not hold'}; "
          f"H-R0.2 {'holds' if ok2 else 'does not hold'}")
    print(f"wrote {os.path.join(res, 'SUMMARY.md')}")


if __name__ == "__main__":
    main()
