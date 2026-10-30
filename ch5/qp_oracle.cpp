// ---------------------------------------------------------------------------
// qp_oracle.cpp - Chapter 5, phase F0: is there anything for query-derived
// pivots to gain?
//
// Before pivots are wired into the Slim-tree (pages, insertion, splits), this
// program measures how much pruning they WOULD add, on exactly the tree and the
// queries of the Chapter 4 `original` arm. It changes nothing in the tree.
//
// For every measured query it runs:
//   1. the real Slim-tree query, to read the true distance and page counts;
//   2. a node-by-node replica of the same search over an in-memory snapshot of
//      the tree, which must reproduce those counts exactly (checked, reported
//      in the `replica_exact` column);
//   3. the same replica with a pivot filter switched on, once per pivot set.
//
// The pivot filter is the one a pivot-enabled tree would apply (PM-tree style),
// placed where it would be placed: after the parent-representative filter and
// before the full distance.
//
//   leaf entry x : prune if |d(q,p) - d(x,p)| > r               for some pivot p
//   index entry e: prune if d(q,p) + r < lo_e(p) or d(q,p) - r > hi_e(p),
//                  [lo_e(p), hi_e(p)] = min/max of d(y,p) over the subtree of e
//
// In k-NN, r is the current radius r_k. A queued subtree is also dropped when
// it is popped if its ring lower bound already exceeds the (smaller) r_k. A
// pruned entry can never be in the answer, so the result and the r_k trajectory
// are unchanged: only the work changes. Every answer is checked by brute force.
// The pivot cost is charged: |P| distances d(q,p) per query.
//
// What is NOT modelled: the space the pivot values take in a page. A real
// pivot-enabled tree has a smaller fan-out (the qp_off control of the plan);
// this pilot isolates the pruning effect on the SAME tree.
//
// Pivot sets, S = 1..--slots pivots each:
//   q_medoids  k-medoids of the adapt queries: the concentration points
//   q_kcenter  farthest-first among the adapt queries, from their 1-medoid
//   d_far      farthest-first over the data, starting from the object farthest
//              from a random one: classic outlier pivots, HF-like
//   d_medoids  k-medoids of a random data sample: central, but query-blind
//   d_random   random data objects
//   q_nearest  ORACLE, S = 1 only: for each measured query, the adapt query
//              nearest to it. Finding it is not charged, so this bounds what a
//              local (per-region) query pivot could ever give.
//   q_nearest_dfar
//              ORACLE, S >= 2: that nearest adapt query plus the first S-1
//              d_far pivots - a local query pivot on top of global data pivots,
//              the shape a region-scoped design (RECAST-like) would take.
//   d_nearest, d_nearest_dfar
//              CONTROLS of the two oracles above: the same construction, but
//              the local pivot is the nearest member of a random DATA sample of
//              the same size as the adapt set. Same budget of candidates; only
//              the queries tell q_* where to put them.
//
// With --hv-out it also computes the index of "homogeneity of viewpoints" of
// Ciaccia, Patella & Zezula (PODS'98, Def. 2), HV = 1 - E[delta(F_Oi, F_Oj)],
// on a sample of objects. HV close to 1 means every object "sees" the data the
// same way, so where a pivot sits matters little.
//
// Build and run:  bash arboretum_SlimTree_QueryGravity/ch5/run_pilot.sh --quick
// ---------------------------------------------------------------------------

// The workload generator, the insertion order and the helpers are taken from
// chapter4.cpp AS IS, by inclusion, so the pilot cannot drift from Chapter 4:
// same Config, same makeWorkload, same seeds. Its main() is renamed away.
#define main chapter4_main_unused
#include "../ch4/chapter4.cpp"
#undef main

#include <limits>
#include <map>
#include <memory>
#include <set>

namespace qp {

typedef stDynamicRPriorityQueue<double, stQueryPriorityQueueValue> tQueue;
const double INF = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// In-memory snapshot of a Slim-tree, read page by page through the page
// manager. Every entry gets a dense id that indexes the pivot columns.
// ---------------------------------------------------------------------------
struct Entry {
    TCity *obj = nullptr;    // owned by the snapshot
    double distToRep = 0.0;  // the page entry's Distance field
    double radius = 0.0;     // index entries only
    u_int32_t child = 0;     // index entries only
    int id = -1;
};

struct Node {
    bool present = false;
    bool leaf = false;
    std::vector<Entry> e;
};

class Snapshot {
  public:
    Snapshot(stPageManager *pm, u_int32_t root) : root_(root) { load(pm, root); }
    ~Snapshot() {
        for (Node &n : nodes_)
            for (Entry &e : n.e) delete e.obj;
    }
    Snapshot(const Snapshot &) = delete;
    Snapshot &operator=(const Snapshot &) = delete;

    const Node &node(u_int32_t page) const { return nodes_[page]; }
    u_int32_t root() const { return root_; }
    int entries() const { return entries_; }
    long leafEntries() const { return leafEntries_; }

  private:
    void load(stPageManager *pm, u_int32_t pageID) {
        if (pageID >= nodes_.size()) nodes_.resize(pageID + 1);
        stPage *page = pm->GetPage(pageID);
        stSlimNode *node = stSlimNode::CreateNode(page);
        Node out;
        out.present = true;
        const u_int32_t n = node->GetNumberOfEntries();
        std::vector<u_int32_t> children;
        if (node->GetNodeType() == stSlimNode::INDEX) {
            stSlimIndexNode *in = (stSlimIndexNode *)node;
            for (u_int32_t i = 0; i < n; i++) {
                Entry e;
                e.obj = new TCity();
                e.obj->Unserialize(in->GetObject(i), in->GetObjectSize(i));
                e.distToRep = in->GetIndexEntry(i).Distance;
                e.radius = in->GetIndexEntry(i).Radius;
                e.child = in->GetIndexEntry(i).PageID;
                e.id = entries_++;
                out.e.push_back(e);
                children.push_back(e.child);
            }
        } else {
            stSlimLeafNode *ln = (stSlimLeafNode *)node;
            out.leaf = true;
            for (u_int32_t i = 0; i < n; i++) {
                Entry e;
                e.obj = new TCity();
                e.obj->Unserialize(ln->GetObject(i), ln->GetObjectSize(i));
                e.distToRep = ln->GetLeafEntry(i).Distance;
                e.id = entries_++;
                out.e.push_back(e);
                leafEntries_++;
            }
        }
        delete node;
        pm->ReleasePage(page);
        nodes_[pageID] = std::move(out);
        for (u_int32_t c : children) load(pm, c);
    }

    u_int32_t root_;
    int entries_ = 0;
    long leafEntries_ = 0;
    std::vector<Node> nodes_;
};

// ---------------------------------------------------------------------------
// One pivot materialised over the whole tree: d(x,p) for every leaf entry and
// the ring [lo, hi] of d(y,p) over the subtree of every index entry. This is
// what the pages of a pivot-enabled tree would hold.
// ---------------------------------------------------------------------------
struct Column {
    TCity *pivot = nullptr;  // not owned
    std::vector<double> leaf, lo, hi;
};

static void fillColumn(const Snapshot &s, u_int32_t page, Column &c,
                       TCityDistanceEvaluator &ev, double &lo, double &hi) {
    const Node &n = s.node(page);
    lo = INF;
    hi = -INF;
    for (const Entry &e : n.e) {
        if (n.leaf) {
            const double d = ev.GetDistance(*e.obj, *c.pivot);
            c.leaf[e.id] = d;
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        } else {
            double clo, chi;
            fillColumn(s, e.child, c, ev, clo, chi);
            c.lo[e.id] = clo;
            c.hi[e.id] = chi;
            lo = std::min(lo, clo);
            hi = std::max(hi, chi);
        }
    }
}

class ColumnCache {
  public:
    ColumnCache(const Snapshot &s, TCityDistanceEvaluator &ev) : s_(s), ev_(ev) {}
    const Column *get(TCity *p) {
        auto it = cols_.find(p);
        if (it != cols_.end()) return it->second.get();
        std::unique_ptr<Column> c(new Column());
        c->pivot = p;
        c->leaf.assign(s_.entries(), -1.0);
        c->lo.assign(s_.entries(), 0.0);
        c->hi.assign(s_.entries(), INF);
        double lo, hi;
        fillColumn(s_, s_.root(), *c, ev_, lo, hi);
        const Column *out = c.get();
        cols_[p] = std::move(c);
        return out;
    }

  private:
    const Snapshot &s_;
    TCityDistanceEvaluator &ev_;
    std::map<TCity *, std::unique_ptr<Column> > cols_;
};

// The pivots seen by one query: their columns and d(q, p_k).
struct PivotView {
    std::vector<const Column *> cols;
    std::vector<double> dq;

    bool empty() const { return cols.empty(); }
    // Lower bound on d(q, y) for every y in the subtree of index entry `id`.
    double ringLB(int id) const {
        double lb = 0.0;
        for (size_t k = 0; k < cols.size(); k++) {
            lb = std::max(lb, cols[k]->lo[id] - dq[k]);
            lb = std::max(lb, dq[k] - cols[k]->hi[id]);
        }
        return lb;
    }
    // Lower bound on d(q, x) for leaf entry `id`.
    double leafLB(int id) const {
        double lb = 0.0;
        for (size_t k = 0; k < cols.size(); k++)
            lb = std::max(lb, std::fabs(dq[k] - cols[k]->leaf[id]));
        return lb;
    }
    double nearest() const {
        double m = INF;
        for (double d : dq) m = std::min(m, d);
        return m;
    }
};

// ---------------------------------------------------------------------------
// Replicas of stSlimTree::RangeQuery and stSlimTree::NearestQuery
// (stSlimTree-inl.h), test for test. With an empty PivotView they must compute
// exactly the distances and read exactly the pages the real tree does.
// ---------------------------------------------------------------------------
struct Counts {
    long indexDist = 0, leafDist = 0, reads = 0;
    long prunedSubtrees = 0, prunedObjects = 0, droppedAtPop = 0;
    std::vector<double> result;  // answer distances, ascending
};

static void rangeRec(const Snapshot &s, u_int32_t page, TCity &q, double r,
                     double dRep, const PivotView &pv, TCityDistanceEvaluator &ev,
                     Counts &c) {
    const Node &n = s.node(page);
    c.reads++;
    if (!n.leaf) {
        for (const Entry &e : n.e) {
            if (std::fabs(dRep - e.distToRep) <= r + e.radius) {
                if (!pv.empty() && pv.ringLB(e.id) > r) {
                    c.prunedSubtrees++;
                    continue;
                }
                const double d = ev.GetDistance(*e.obj, q);
                c.indexDist++;
                if (d <= r + e.radius) rangeRec(s, e.child, q, r, d, pv, ev, c);
            }
        }
    } else {
        for (const Entry &e : n.e) {
            if (std::fabs(dRep - e.distToRep) <= r) {
                if (!pv.empty() && pv.leafLB(e.id) > r) {
                    c.prunedObjects++;
                    continue;
                }
                const double d = ev.GetDistance(*e.obj, q);
                c.leafDist++;
                if (d <= r) c.result.push_back(d);
            }
        }
    }
}

static Counts simRange(const Snapshot &s, TCity &q, double r, const PivotView &pv,
                       TCityDistanceEvaluator &ev) {
    Counts c;
    const Node &root = s.node(s.root());
    c.reads++;
    // The public RangeQuery has no parent filter at the root.
    for (const Entry &e : root.e) {
        if (!root.leaf) {
            if (!pv.empty() && pv.ringLB(e.id) > r) {
                c.prunedSubtrees++;
                continue;
            }
            const double d = ev.GetDistance(*e.obj, q);
            c.indexDist++;
            if (d <= r + e.radius) rangeRec(s, e.child, q, r, d, pv, ev, c);
        } else {
            if (!pv.empty() && pv.leafLB(e.id) > r) {
                c.prunedObjects++;
                continue;
            }
            const double d = ev.GetDistance(*e.obj, q);
            c.leafDist++;
            if (d <= r) c.result.push_back(d);
        }
    }
    std::sort(c.result.begin(), c.result.end());
    return c;
}

static Counts simKnn(const Snapshot &s, TCity &q, unsigned int k,
                     const PivotView &pv, TCityDistanceEvaluator &ev) {
    Counts c;
    double rangeK = MAXDOUBLE;
    double distanceRepres = 0.0, distance = 0.0;
    std::unique_ptr<tQueue> queue(new tQueue(STARTVALUEQUEUE, INCREMENTVALUEQUEUE));
    std::map<u_int32_t, double> ringOfPage;  // a page is queued at most once
    stQueryPriorityQueueValue cur, tmp;
    cur.PageID = s.root();
    cur.Radius = 0;

    while (cur.PageID != 0) {
        const Node &n = s.node(cur.PageID);
        c.reads++;
        if (!n.leaf) {
            for (const Entry &e : n.e) {
                if (std::fabs(distanceRepres - e.distToRep) <= rangeK + e.radius) {
                    double lb = 0.0;
                    if (!pv.empty()) {
                        lb = pv.ringLB(e.id);
                        if (lb > rangeK) {
                            c.prunedSubtrees++;
                            continue;
                        }
                    }
                    distance = ev.GetDistance(*e.obj, q);
                    c.indexDist++;
                    if (distance <= rangeK + e.radius) {
                        tmp.PageID = e.child;
                        tmp.Radius = e.radius;
                        queue->Add(distance, tmp);
                        ringOfPage[e.child] = lb;
                    }
                }
            }
        } else {
            for (const Entry &e : n.e) {
                if (std::fabs(distanceRepres - e.distToRep) <= rangeK) {
                    if (!pv.empty() && pv.leafLB(e.id) > rangeK) {
                        c.prunedObjects++;
                        continue;
                    }
                    distance = ev.GetDistance(*e.obj, q);
                    c.leafDist++;
                    if (distance <= rangeK) {
                        // stResult::AddPair + Cut(k): keep the k smallest.
                        c.result.insert(std::upper_bound(c.result.begin(),
                                                         c.result.end(), distance),
                                        distance);
                        if (c.result.size() >= k) {
                            c.result.resize(k);
                            rangeK = c.result.back();
                        }
                    }
                }
            }
        }
        bool stop = false;
        do {
            if (queue->Get(distance, cur)) {
                if (distance <= rangeK + cur.Radius) {
                    if (!pv.empty() && ringOfPage[cur.PageID] > rangeK) {
                        c.droppedAtPop++;  // r_k shrank below the ring bound
                    } else {
                        distanceRepres = distance;
                        stop = true;
                    }
                }
            } else {
                cur.PageID = 0;
                stop = true;
            }
        } while (!stop);
    }
    return c;
}

// ---------------------------------------------------------------------------
// Pivot selection
// ---------------------------------------------------------------------------
typedef std::vector<std::vector<double> > Matrix;

static Matrix distanceMatrix(const std::vector<TCity *> &v, TCityDistanceEvaluator &ev) {
    Matrix D(v.size(), std::vector<double>(v.size(), 0.0));
    for (size_t i = 0; i < v.size(); i++)
        for (size_t j = i + 1; j < v.size(); j++)
            D[i][j] = D[j][i] = ev.GetDistance(*v[i], *v[j]);
    return D;
}

static int oneMedoid(const Matrix &D) {
    int best = 0;
    double bestSum = INF;
    for (size_t i = 0; i < D.size(); i++) {
        double sum = 0.0;
        for (double d : D[i]) sum += d;
        if (sum < bestSum) {
            bestSum = sum;
            best = (int)i;
        }
    }
    return best;
}

// Farthest-first traversal from `start`; the picks are nested in k.
static std::vector<int> farthestFirst(const Matrix &D, int start, int k) {
    std::vector<int> picks;
    if (D.empty() || k <= 0) return picks;
    std::vector<double> best(D.size(), INF);
    picks.push_back(start);
    while ((int)picks.size() < std::min<int>(k, (int)D.size())) {
        const int last = picks.back();
        int far = 0;
        double farDist = -1.0;
        for (size_t i = 0; i < D.size(); i++) {
            best[i] = std::min(best[i], D[i][last]);
            if (best[i] > farDist) {
                farDist = best[i];
                far = (int)i;
            }
        }
        picks.push_back(far);
    }
    return picks;
}

// k-medoids by Voronoi iteration, started from farthest-first around the
// 1-medoid. Deterministic: ties go to the lowest index.
static std::vector<int> kMedoids(const Matrix &D, int k) {
    const int n = (int)D.size();
    k = std::min(k, n);
    std::vector<int> med = farthestFirst(D, oneMedoid(D), k);
    std::vector<int> owner(n, 0);
    for (int iter = 0; iter < 100; iter++) {
        for (int i = 0; i < n; i++) {
            int b = 0;
            for (int j = 1; j < k; j++)
                if (D[i][med[j]] < D[i][med[b]]) b = j;
            owner[i] = b;
        }
        bool changed = false;
        for (int j = 0; j < k; j++) {
            int best = med[j];
            double bestSum = INF;
            for (int a = 0; a < n; a++) {
                if (owner[a] != j) continue;
                double sum = 0.0;
                for (int b = 0; b < n; b++)
                    if (owner[b] == j) sum += D[a][b];
                if (sum < bestSum) {
                    bestSum = sum;
                    best = a;
                }
            }
            if (best != med[j]) {
                med[j] = best;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return med;
}

// Farthest-first over the whole data set, without a full matrix. The first
// pivot is the object farthest from a random one, so it sits on the border.
static std::vector<TCity *> farthestFirstData(const std::vector<TCity *> &objects,
                                              int k, unsigned int seed,
                                              TCityDistanceEvaluator &ev) {
    std::vector<TCity *> picks;
    if (objects.empty() || k <= 0) return picks;
    std::mt19937 rng(seed);
    TCity *start = objects[rng() % objects.size()];
    size_t first = 0;
    double firstDist = -1.0;
    for (size_t i = 0; i < objects.size(); i++) {
        const double d = ev.GetDistance(*objects[i], *start);
        if (d > firstDist) {
            firstDist = d;
            first = i;
        }
    }
    picks.push_back(objects[first]);
    std::vector<double> best(objects.size(), INF);
    while ((int)picks.size() < k) {
        TCity *last = picks.back();
        size_t far = 0;
        double farDist = -1.0;
        for (size_t i = 0; i < objects.size(); i++) {
            best[i] = std::min(best[i], ev.GetDistance(*objects[i], *last));
            if (best[i] > farDist) {
                farDist = best[i];
                far = i;
            }
        }
        picks.push_back(objects[far]);
    }
    return picks;
}

static std::vector<TCity *> pick(const std::vector<TCity *> &from,
                                 const std::vector<int> &idx, int k) {
    std::vector<TCity *> out;
    for (int i = 0; i < k && i < (int)idx.size(); i++) out.push_back(from[idx[i]]);
    return out;
}

struct PivotSet {
    std::string name;
    int slots = 0;
    std::vector<TCity *> pivots;  // not owned
};

// ---------------------------------------------------------------------------
// Homogeneity of viewpoints (PODS'98, Section 2)
//   F_Oi(x) = Pr{d(Oi, O) <= x}, the relative distance distribution of Oi
//   delta(F_Oi, F_Oj) = (1/d+) * integral_0^d+ |F_Oi(x) - F_Oj(x)| dx
//   HV = 1 - E[delta]
// The integral is a midpoint sum over a uniform grid on [0, d+], with d+ the
// largest distance seen from the sample.
// ---------------------------------------------------------------------------
static void homogeneity(const std::vector<TCity *> &objects, int sampleSize,
                        unsigned int seed, const std::string &label,
                        const std::string &path) {
    TCityDistanceEvaluator ev;
    std::vector<size_t> idx(objects.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    std::mt19937 rng(seed);
    std::shuffle(idx.begin(), idx.end(), rng);
    const int m = std::min<int>(sampleSize, (int)objects.size());

    std::vector<std::vector<double> > rdd(m);
    double dplus = 0.0;
    for (int i = 0; i < m; i++) {
        rdd[i].reserve(objects.size());
        for (TCity *o : objects) rdd[i].push_back(ev.GetDistance(*objects[idx[i]], *o));
        std::sort(rdd[i].begin(), rdd[i].end());
        dplus = std::max(dplus, rdd[i].back());
    }
    const int grid = 1000;
    std::vector<std::vector<double> > F(m, std::vector<double>(grid));
    for (int i = 0; i < m; i++)
        for (int b = 0; b < grid; b++) {
            const double x = (b + 0.5) * dplus / grid;
            F[i][b] = (double)(std::upper_bound(rdd[i].begin(), rdd[i].end(), x) -
                               rdd[i].begin()) /
                      (double)rdd[i].size();
        }
    std::vector<double> deltas;
    for (int i = 0; i < m; i++)
        for (int j = i + 1; j < m; j++) {
            double sum = 0.0;
            for (int b = 0; b < grid; b++) sum += std::fabs(F[i][b] - F[j][b]);
            deltas.push_back(sum / grid);
        }
    const double meanDelta = ch4::mean(deltas);
    std::ofstream os;
    ch4::openCsv(os, path,
                 "dataset,objects,sample,seed,grid,d_plus,hv,delta_mean,"
                 "delta_p10,delta_p50,delta_p90,delta_max");
    os << label << ',' << objects.size() << ',' << m << ',' << seed << ',' << grid
       << ',' << dplus << ',' << 1.0 - meanDelta << ',' << meanDelta << ','
       << ch4::percentile(deltas, 0.10) << ',' << ch4::percentile(deltas, 0.50)
       << ',' << ch4::percentile(deltas, 0.90) << ','
       << ch4::percentile(deltas, 1.0) << "\n";
    std::cout << "HV(" << label << ") = " << 1.0 - meanDelta << " over " << m
              << " objects (mean discrepancy " << meanDelta << ", d+ = " << dplus
              << ")\n";
}

// ---------------------------------------------------------------------------
// Per-configuration accumulators for one batch
// ---------------------------------------------------------------------------
struct Sums {
    long queries = 0;
    double indexDist = 0, leafDist = 0, pivotDist = 0, reads = 0;
    double prunedSubtrees = 0, prunedObjects = 0, droppedAtPop = 0;
    double queryToPivot = 0;
    bool correct = true;

    void add(const Counts &c, double pivots, double nearest) {
        queries++;
        indexDist += c.indexDist;
        leafDist += c.leafDist;
        pivotDist += pivots;
        reads += c.reads;
        prunedSubtrees += c.prunedSubtrees;
        prunedObjects += c.prunedObjects;
        droppedAtPop += c.droppedAtPop;
        queryToPivot += nearest;
    }
    double distances() const { return indexDist + leafDist + pivotDist; }
};

static const char *PILOT_HEADER =
    "dataset,regime,query_type,k,radius,repetition,seed,set,slots,queries,"
    "base_distances,base_reads,distances,index_distances,leaf_distances,"
    "pivot_distances,reads,saving_distances_pct,saving_reads_pct,"
    "pruned_subtrees,pruned_objects,dropped_at_pop,query_to_pivot,"
    "real_distances,real_reads,replica_exact,correct";

}  // namespace qp

// ---------------------------------------------------------------------------
static void usage() {
    std::cout <<
        "qp_oracle - Chapter 5 pilot: how much would query-derived pivots prune?\n"
        "\n"
        "  --dataset F --queryfile F   cities file and uniform query pool (required)\n"
        "  --regime R                  concentrado | disperso | uniforme\n"
        "  --query-type T              knn | range | both            (both)\n"
        "  --reps N --rep-offset N --seed N                          (1, 0, 42)\n"
        "  --adapt-queries N --measure-queries N                     (200, 100)\n"
        "  --k N --radius R --page-size N --region-factor F          (10, 0.5, 1024, 3)\n"
        "  --index-dir D               where the temporary index file goes (.)\n"
        "  --slots N                   largest pivot set size S      (4)\n"
        "  --sets LIST                 comma-separated subset of\n"
        "                              q_medoids,q_kcenter,d_far,d_medoids,d_random,\n"
        "                              q_nearest,q_nearest_dfar,d_nearest,d_nearest_dfar\n"
        "  --data-sample N             data sample for d_medoids     (1000)\n"
        "  --pilot-out F --pivots-out F   CSV outputs (appended)\n"
        "  --hv-out F --hv-sample N    homogeneity of viewpoints     (200 objects)\n"
        "  --hv-only                   compute HV and exit\n"
        "  --dataset-label S --no-verify\n";
}

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    Config cfg;
    int slots = 4;
    int dataSample = 1000;
    int hvSample = 200;
    bool hvOnly = false;
    std::string sets =
        "q_medoids,q_kcenter,d_far,d_medoids,d_random,"
        "q_nearest,q_nearest_dfar,d_nearest,d_nearest_dfar";
    std::string pilotOut, pivotsOut, hvOut;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "ERROR: " << what << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--dataset") cfg.dataset = next("--dataset");
        else if (a == "--dataset-label") cfg.datasetLabel = next("--dataset-label");
        else if (a == "--queryfile") cfg.queryFile = next("--queryfile");
        else if (a == "--index-dir") cfg.indexDir = next("--index-dir");
        else if (a == "--regime") cfg.regime = next("--regime");
        else if (a == "--query-type") cfg.queryType = next("--query-type");
        else if (a == "--page-size") cfg.pageSize = (unsigned)std::stoul(next("--page-size"));
        else if (a == "--k") cfg.k = (unsigned)std::stoul(next("--k"));
        else if (a == "--radius") cfg.radius = std::stod(next("--radius"));
        else if (a == "--adapt-queries") cfg.adaptQueries = std::stoi(next("--adapt-queries"));
        else if (a == "--measure-queries") cfg.measureQueries = std::stoi(next("--measure-queries"));
        else if (a == "--reps") cfg.reps = std::stoi(next("--reps"));
        else if (a == "--rep-offset") cfg.repOffset = std::stoi(next("--rep-offset"));
        else if (a == "--seed") cfg.seed = (unsigned)std::stoul(next("--seed"));
        else if (a == "--centres") cfg.hotspots = std::stoi(next("--centres"));
        else if (a == "--region-factor") cfg.regionFactor = std::stod(next("--region-factor"));
        else if (a == "--no-verify") cfg.verify = false;
        else if (a == "--slots") slots = std::stoi(next("--slots"));
        else if (a == "--sets") sets = next("--sets");
        else if (a == "--data-sample") dataSample = std::stoi(next("--data-sample"));
        else if (a == "--pilot-out") pilotOut = next("--pilot-out");
        else if (a == "--pivots-out") pivotsOut = next("--pivots-out");
        else if (a == "--hv-out") hvOut = next("--hv-out");
        else if (a == "--hv-sample") hvSample = std::stoi(next("--hv-sample"));
        else if (a == "--hv-only") hvOnly = true;
        else {
            std::cerr << "ERROR: unknown option " << a << "\n";
            return 2;
        }
    }
    if (cfg.dataset.empty() || (!hvOnly && cfg.queryFile.empty())) {
        std::cerr << "ERROR: --dataset and --queryfile are required\n";
        return 2;
    }
    if (cfg.regime != "concentrado" && cfg.regime != "disperso" &&
        cfg.regime != "uniforme") {
        std::cerr << "ERROR: unknown regime: " << cfg.regime << "\n";
        return 2;
    }
    if (slots < 1 || slots > 16) {
        std::cerr << "ERROR: --slots must lie in [1, 16]\n";
        return 2;
    }
    auto wanted = [&](const std::string &name) {
        return ("," + sets + ",").find("," + name + ",") != std::string::npos;
    };

    std::vector<TCity *> objects = ch4::loadCities(cfg.dataset);
    if (!hvOut.empty()) qp::homogeneity(objects, hvSample, cfg.seed, cfg.datasetLabel, hvOut);
    if (hvOnly) {
        ch4::deleteAll(objects);
        return 0;
    }
    std::vector<TCity *> pool = ch4::loadCities(cfg.queryFile);

    std::vector<std::string> queryTypes;
    if (cfg.queryType == "both") {
        queryTypes.push_back("knn");
        queryTypes.push_back("range");
    } else {
        queryTypes.push_back(cfg.queryType);
    }

    std::ofstream pilotFile, pivotsFile;
    ch4::openCsv(pilotFile, pilotOut, qp::PILOT_HEADER);
    ch4::openCsv(pivotsFile, pivotsOut,
                 "dataset,regime,repetition,seed,set,slots,slot,name,latitude,longitude");

    // Separate evaluators, so no bookkeeping distance lands in a measured count.
    TCityDistanceEvaluator genEval, verifyEval, pivotEval, simEval, oracleEval;
    int failures = 0;

    for (int r = 0; r < cfg.reps; r++) {
        const int rep = cfg.repOffset + r;
        const unsigned int repSeed = cfg.seed + (unsigned int)rep;

        // Exactly the workload and the insertion order of chapter4.cpp.
        const Workload w = makeWorkload(cfg, genEval, objects, pool, repSeed);
        std::vector<TCity *> order(objects);
        {
            std::mt19937 orderRng(repSeed + 1000003u);
            std::shuffle(order.begin(), order.end(), orderRng);
        }
        std::cout << "regime " << cfg.regime << " | rep " << rep << " seed "
                  << repSeed << " | adapt " << w.adapt.size() << " measure "
                  << w.measure.size() << "\n";

        const std::string idxPath =
            cfg.indexDir + "/qp_oracle_" + cfg.regime + ".dat";
        std::error_code ec;
        std::filesystem::remove(idxPath, ec);
        stPlainDiskPageManager *pm =
            new stPlainDiskPageManager(idxPath.c_str(), cfg.pageSize);
        mySlimTree *tree = new mySlimTree(pm);
        for (TCity *o : order) tree->Add(o);

        qp::Snapshot snap(pm, tree->GetRootPageID());
        if (snap.leafEntries() != (long)objects.size()) {
            std::cerr << "ERROR: snapshot has " << snap.leafEntries()
                      << " leaf entries, expected " << objects.size() << "\n";
            return 1;
        }
        qp::ColumnCache columns(snap, pivotEval);

        // ---- pivot sets -----------------------------------------------------
        std::vector<qp::PivotSet> configs;
        auto addSet = [&](const std::string &name, int s,
                          const std::vector<TCity *> &pv) {
            qp::PivotSet ps;
            ps.name = name;
            ps.slots = s;
            ps.pivots = pv;
            configs.push_back(ps);
        };
        const qp::Matrix Dadapt = qp::distanceMatrix(w.adapt, pivotEval);
        const std::vector<int> qCenter =
            qp::farthestFirst(Dadapt, qp::oneMedoid(Dadapt), slots);
        const std::vector<TCity *> dFar =
            qp::farthestFirstData(objects, slots, repSeed + 2000003u, pivotEval);
        std::vector<TCity *> sample(objects);
        {
            std::mt19937 sampleRng(repSeed + 3000017u);
            std::shuffle(sample.begin(), sample.end(), sampleRng);
            if ((int)sample.size() > dataSample) sample.resize(dataSample);
        }
        const qp::Matrix Dsample =
            wanted("d_medoids") ? qp::distanceMatrix(sample, pivotEval) : qp::Matrix();
        std::vector<TCity *> shuffled(objects);
        {
            std::mt19937 randomRng(repSeed + 4000037u);
            std::shuffle(shuffled.begin(), shuffled.end(), randomRng);
        }
        for (int s = 1; s <= slots; s++) {
            if (wanted("q_medoids"))
                addSet("q_medoids", s, qp::pick(w.adapt, qp::kMedoids(Dadapt, s), s));
            if (wanted("q_kcenter")) addSet("q_kcenter", s, qp::pick(w.adapt, qCenter, s));
            if (wanted("d_far"))
                addSet("d_far", s, std::vector<TCity *>(dFar.begin(), dFar.begin() + s));
            if (wanted("d_medoids"))
                addSet("d_medoids", s, qp::pick(sample, qp::kMedoids(Dsample, s), s));
            if (wanted("d_random"))
                addSet("d_random", s,
                       std::vector<TCity *>(shuffled.begin(), shuffled.begin() + s));
        }
        for (const qp::PivotSet &ps : configs)
            for (size_t j = 0; j < ps.pivots.size(); j++)
                if (pivotsFile.is_open())
                    pivotsFile << cfg.datasetLabel << ',' << cfg.regime << ',' << rep
                               << ',' << repSeed << ',' << ps.name << ',' << ps.slots
                               << ',' << j << ",\"" << ps.pivots[j]->GetName() << "\","
                               << ps.pivots[j]->GetLatitude() << ','
                               << ps.pivots[j]->GetLongitude() << "\n";

        // The local oracles. For each measured query, the nearest member of a
        // pool of candidate pivots: the adapt queries (q_*) or, as their
        // control, a random data sample of the SAME size (d_*). Same budget of
        // candidates; only the queries tell the first pool where to be.
        struct LocalPool {
            std::string prefix;
            std::vector<TCity *> cand, nearest;  // not owned
            qp::Sums one;                        // S = 1
            std::vector<qp::Sums> withFar;       // index = S, local + S-1 d_far
        };
        std::vector<LocalPool> pools;
        {
            std::vector<TCity *> dataPool(objects);
            std::mt19937 poolRng(repSeed + 5000011u);
            std::shuffle(dataPool.begin(), dataPool.end(), poolRng);
            dataPool.resize(std::min(dataPool.size(), w.adapt.size()));
            const std::pair<const char *, const std::vector<TCity *> *> src[] = {
                {"q", &w.adapt}, {"d", &dataPool}};
            for (const auto &s : src) {
                const std::string p = s.first;
                if (!wanted(p + "_nearest") && !wanted(p + "_nearest_dfar")) continue;
                LocalPool lp;
                lp.prefix = p;
                lp.cand = *s.second;
                for (TCity *m : w.measure) {
                    TCity *best = lp.cand.empty() ? nullptr : lp.cand[0];
                    double bestDist = INFINITY;
                    for (TCity *a : lp.cand) {
                        const double d = oracleEval.GetDistance(*m, *a);
                        if (d < bestDist) {
                            bestDist = d;
                            best = a;
                        }
                    }
                    lp.nearest.push_back(best);
                }
                pools.push_back(lp);
            }
        }

        // ---- measured queries ----------------------------------------------
        for (const std::string &qtype : queryTypes) {
            const bool knn = (qtype == "knn");
            qp::Sums base, real;
            std::vector<qp::Sums> sums(configs.size());
            for (LocalPool &lp : pools) {
                lp.one = qp::Sums();
                lp.withFar.assign(slots + 1, qp::Sums());
            }
            bool replicaExact = true;

            auto run = [&](TCity *q, const qp::PivotView &pv) {
                return knn ? qp::simKnn(snap, *q, cfg.k, pv, simEval)
                           : qp::simRange(snap, *q, cfg.radius, pv, simEval);
            };

            for (size_t i = 0; i < w.measure.size(); i++) {
                TCity *q = w.measure[i];
                std::vector<double> want;
                if (cfg.verify)
                    want = knn ? ch4::bruteKnn(verifyEval, objects, q, cfg.k)
                               : ch4::bruteRange(verifyEval, objects, q, cfg.radius);
                auto check = [&](const qp::Counts &c) {
                    return !cfg.verify || ch4::sameDistances(c.result, want);
                };

                // 1. The real tree.
                const long d0 = (long)tree->GetMetricEvaluator()->GetDistanceCount();
                const long r0 = (long)pm->GetReadCount();
                myResult *res = knn ? tree->NearestQuery(q, cfg.k)
                                    : tree->RangeQuery(q, cfg.radius);
                const long realDist =
                    (long)tree->GetMetricEvaluator()->GetDistanceCount() - d0;
                const long realReads = (long)pm->GetReadCount() - r0;
                real.queries++;
                real.leafDist += realDist;  // index/leaf split is not needed here
                real.reads += realReads;
                if (cfg.verify && !ch4::sameDistances(ch4::resultDistances(res), want))
                    real.correct = false;
                delete res;

                // 2. The replica, no pivots: must match the real tree exactly.
                const qp::PivotView none;
                const qp::Counts b = run(q, none);
                base.add(b, 0, 0);
                if (!check(b)) base.correct = false;
                if (b.indexDist + b.leafDist != realDist || b.reads != realReads) {
                    if (replicaExact)
                        std::cerr << "WARNING: replica differs from the tree on query "
                                  << i << " (" << qtype << "): distances "
                                  << b.indexDist + b.leafDist << " vs " << realDist
                                  << ", reads " << b.reads << " vs " << realReads << "\n";
                    replicaExact = false;
                }

                // 3. The replica with each pivot set.
                for (size_t c = 0; c < configs.size(); c++) {
                    qp::PivotView pv;
                    for (TCity *p : configs[c].pivots) {
                        pv.cols.push_back(columns.get(p));
                        pv.dq.push_back(simEval.GetDistance(*q, *p));
                    }
                    const qp::Counts pc = run(q, pv);
                    sums[c].add(pc, (double)pv.cols.size(), pv.nearest());
                    if (!check(pc)) sums[c].correct = false;
                }
                for (LocalPool &lp : pools) {
                    qp::PivotView pv;
                    pv.cols.push_back(columns.get(lp.nearest[i]));
                    pv.dq.push_back(simEval.GetDistance(*q, *lp.nearest[i]));
                    if (wanted(lp.prefix + "_nearest")) {
                        const qp::Counts pc = run(q, pv);
                        lp.one.add(pc, 1.0, pv.nearest());
                        if (!check(pc)) lp.one.correct = false;
                    }
                    if (wanted(lp.prefix + "_nearest_dfar"))
                        for (int s = 2; s <= slots; s++) {
                            pv.cols.push_back(columns.get(dFar[s - 2]));
                            pv.dq.push_back(simEval.GetDistance(*q, *dFar[s - 2]));
                            const qp::Counts pc = run(q, pv);
                            // queryToPivot = distance to the LOCAL pivot.
                            lp.withFar[s].add(pc, (double)s, pv.dq[0]);
                            if (!check(pc)) lp.withFar[s].correct = false;
                        }
                }
            }

            const double n = base.queries > 0 ? (double)base.queries : 1.0;
            const double baseDist = base.distances() / n, baseReads = base.reads / n;
            auto row = [&](const std::string &set, int s, const qp::Sums &x) {
                const double nq = x.queries > 0 ? (double)x.queries : 1.0;
                const double dist = x.distances() / nq, reads = x.reads / nq;
                const bool ok = x.correct && base.correct && real.correct;
                if (!ok) failures++;
                if (!pilotFile.is_open()) return;
                pilotFile << cfg.datasetLabel << ',' << cfg.regime << ',' << qtype << ','
                          << cfg.k << ',' << cfg.radius << ',' << rep << ',' << repSeed
                          << ',' << set << ',' << s << ',' << x.queries << ','
                          << baseDist << ',' << baseReads << ',' << dist << ','
                          << x.indexDist / nq << ',' << x.leafDist / nq << ','
                          << x.pivotDist / nq << ',' << reads << ','
                          << 100.0 * (baseDist - dist) / baseDist << ','
                          << 100.0 * (baseReads - reads) / baseReads << ','
                          << x.prunedSubtrees / nq << ',' << x.prunedObjects / nq << ','
                          << x.droppedAtPop / nq << ',';
                if (s > 0) pilotFile << x.queryToPivot / nq;
                pilotFile << ',' << real.leafDist / n << ',' << real.reads / n << ','
                          << (replicaExact ? 1 : 0) << ',' << (ok ? 1 : 0) << "\n";
            };
            row("none", 0, base);
            for (size_t c = 0; c < configs.size(); c++)
                row(configs[c].name, configs[c].slots, sums[c]);
            for (const LocalPool &lp : pools) {
                if (wanted(lp.prefix + "_nearest")) row(lp.prefix + "_nearest", 1, lp.one);
                if (wanted(lp.prefix + "_nearest_dfar"))
                    for (int s = 2; s <= slots; s++)
                        row(lp.prefix + "_nearest_dfar", s, lp.withFar[s]);
            }

            // One console line per batch: the best set of each family at S = slots.
            std::cout << "  " << qtype << ": base " << baseDist << " dist, "
                      << baseReads << " reads | replica "
                      << (replicaExact ? "exact" : "DIFFERS");
            for (size_t c = 0; c < configs.size(); c++)
                if (configs[c].slots == slots)
                    std::cout << " | " << configs[c].name << " "
                              << 100.0 * (baseDist - sums[c].distances() / n) / baseDist
                              << "%";
            for (const LocalPool &lp : pools) {
                if (wanted(lp.prefix + "_nearest"))
                    std::cout << " | " << lp.prefix << "_nearest(S=1) "
                              << 100.0 * (baseDist - lp.one.distances() / n) / baseDist
                              << "%";
                if (wanted(lp.prefix + "_nearest_dfar") && slots >= 2)
                    std::cout << " | " << lp.prefix << "_nearest_dfar "
                              << 100.0 * (baseDist - lp.withFar[slots].distances() / n) /
                                     baseDist
                              << "%";
            }
            std::cout << "\n";
            if (!replicaExact) failures++;
        }

        delete tree;
        delete pm;
        std::filesystem::remove(idxPath, ec);
    }

    ch4::deleteAll(objects);
    ch4::deleteAll(pool);
    if (failures) {
        std::cerr << "ERROR: " << failures
                  << " batch(es) failed verification or replica check\n";
        return 1;
    }
    return 0;
}
