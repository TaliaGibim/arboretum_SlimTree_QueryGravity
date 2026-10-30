// ---------------------------------------------------------------------------
// qp_recast.cpp - Chapter 5, phase R0: RECAST-style query pivots inside the
// Slim-tree, measured without changing the tree.
//
// RECAST (Liu & Mao, arXiv 2608.06962) keeps, per region, a table of "paid"
// pivot columns: the distances d(x, q*) that a past query q* computed anyway
// while it was being answered. Here a region is a Slim-tree node. When a query
// finishes, every node it read may store one column for it. The column holds,
// for each entry of the node, an interval [lo, hi] that contains d(y, q*) for
// every object y of the entry: the object itself in a leaf, the whole subtree in
// an index node. Building it costs no distance at all:
//
//   computed d(q*, x), leaf entry           -> [d, d]
//   computed d(q*, rep_e), index entry      -> [d - R_e, d + R_e]
//   skipped by the parent filter            -> triangle inequality through the
//                                              node representative, whose two
//                                              distances are both known
//   pruned by another pivot                 -> the bound that pivot gave
//   index entry whose child was read too    -> also [min lo, max hi] of the
//                                              child's own intervals
//
// A later query q prunes entry e with pivot p when d(q,p) + r < lo or
// d(q,p) - r > hi; in k-NN, r is the current r_k. d(q,p) is computed lazily,
// once per query, shared by every node that holds p, and charged to the query.
// It is not computed at all when the node representative already settles that
// p cannot prune the entry: the slot keeps d(p, rep), the query knows
// d(q, rep), so d(q,p) lies in [|d(q,rep) - d(p,rep)|, d(q,rep) + d(p,rep)]
// (--no-rep-skip turns this off). Pivots are tried in decreasing order of gain,
// and the first one that prunes gets the credit.
//
// Life cycle per node (RECAST Sec. III-B/C, adapted):
//   admission  the query computed >= c_min distances in the node and >= f_min
//              of them were false positives (not in the answer): trial pivot
//   gain       pruned work (1 per leaf entry; 1 + c^ * leaves below per index
//              entry, c^ = leaf distances per leaf entry read) minus its share
//              of d(q,p); forgotten at rate --decay per visit of the node
//              (0 = RECAST's cumulative gain)
//   confirmed  when gain > tau; demoted back to trial when gain < 0
//   eviction   (table at K slots or at K_n trials)
//                cost falling (c < EMA) or first visit: only a trial whose
//                                                       gain is <= 0
//                cost spike (c > kappa * EMA):          the oldest pivot
//                otherwise:                             the lowest gain per
//                                                       stored value (trials
//                                                       penalised)
//
// What R0 does NOT model: insertion (the tree is static, as in the F0 pilot) and
// where the tables live (in memory, as in RECAST; their size is reported).
//
// Arms, per repetition, regime and query type, all on the same tree:
//   none         the Slim-tree (replica, checked against the real tree)
//   recast       tables built online by the adapt queries; frozen for the
//                measured queries
//   recast_live  the same tables, still learning during the measured queries
//   recast_data  CONTROL: tables built by as many "queries" centred on random
//                cities (their whole cost is overhead), then frozen
//   oracle_q     F0 ceiling: per query, the FULL column of its nearest adapt
//                query (200 full columns, nearest found for free)
//   oracle_d     its control: the nearest member of a random data sample
//   pm_dfar4     reference: 4 global data pivots, PM-tree style (installation
//                cost and page space not charged)
//
// Build and run:  bash arboretum_SlimTree_QueryGravity/ch5/run_recast.sh --quick
// ---------------------------------------------------------------------------
#define QP_ORACLE_NO_MAIN
#include "qp_oracle.cpp"

#include <functional>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <unordered_map>

namespace rc {

using qp::INF;
// Safety margin of every pivot test, far above the rounding error of the
// distances (degrees, below 100).
const double EPS = 1e-9;

struct Params {
    int nodeSlots = 8;    // K: pivots per node
    int trialCap = 4;     // K_n: trial pivots per node
    double kappa = 4.0;   // cost spike: c > kappa * EMA
    double lambda = 0.1;  // EMA weight of the node cost and of c^
    double decay = 0.05;  // gain forgetting per node visit; 0 = cumulative
    double tau = 1.0;     // trial -> confirmed when gain > tau
    int cMin = 1;         // admission: distances computed in the node
    int fMin = 1;         // admission: false positives in the node
    bool repSkip = true;  // skip a pivot that cannot prune, without d(q,p)
};

struct Slot {
    int pivot = -1;              // index into Engine::pivots_
    std::vector<double> lo, hi;  // per entry of the node
    int known = 0;               // entries whose distance the pivot computed
    double gain = 0.0;
    bool confirmed = false;
    long birth = 0;
    double repDist = -1.0;  // d(p, representative of the node); -1 at the root
    double efficiency() const {
        return gain / (double)std::max(1, known) - (confirmed ? 0.0 : 1.0);
    }
};

struct Table {
    std::vector<Slot> slots;
    double ema = 0.0;  // EMA of the distances computed in the node per visit
    long visits = 0;   // learning visits
};

// What one query did in one node: the material of its paid column.
struct Visit {
    u_int32_t page = 0;
    bool hasRep = false;
    double dRep = 0.0;  // d(q, representative of the node)
    std::vector<double> lo, hi;  // bounds on d(q, y), y in the entry
    std::vector<char> exact;
    std::vector<double> dist;  // the distance, when computed
    std::vector<int> child;    // visit index of the child node, or -1
    int computed = 0;
    int falsePos = 0;
};

struct Totals {
    long admitted = 0, evicted = 0, refused = 0, confirmations = 0, demotions = 0;
};

class Engine {
  public:
    Engine(const qp::Snapshot &s, const std::vector<int> &leafCount, const Params &p)
        : s_(&s), leafCount_(&leafCount), p_(p) {}

    // One query. learn = update the gains, admit the paid column, evict.
    qp::Counts range(TCity &q, double r, bool learn, long &pivotDist) {
        Ctx c(q, learn);
        qp::Counts out;
        rangeNode(c, s_->root(), false, 0.0, r, out);
        std::sort(out.result.begin(), out.result.end());
        finish(c, r, out);
        pivotDist = c.pivotDist;
        return out;
    }

    // Replica of stSlimTree::NearestQuery (see qp::simKnn) with the tables.
    qp::Counts knn(TCity &q, unsigned int k, bool learn, long &pivotDist) {
        Ctx c(q, learn);
        qp::Counts out;
        double rangeK = MAXDOUBLE, distanceRepres = 0.0, distance = 0.0;
        std::unique_ptr<qp::tQueue> queue(
            new qp::tQueue(STARTVALUEQUEUE, INCREMENTVALUEQUEUE));
        std::map<u_int32_t, double> ringOfPage;
        std::map<u_int32_t, std::pair<int, size_t> > parentOf;
        stQueryPriorityQueueValue cur, tmp;
        cur.PageID = s_->root();
        cur.Radius = 0;
        bool hasRep = false;  // the root has no representative

        while (cur.PageID != 0) {
            const u_int32_t page = cur.PageID;
            const qp::Node &n = s_->node(page);
            out.reads++;
            const int vi = enter(c, page, n, hasRep, distanceRepres);
            auto pit = parentOf.find(page);
            if (pit != parentOf.end())
                c.visits[pit->second.first].child[pit->second.second] = vi;
            Table *t = tableAt(page);
            const std::vector<int> order = orderOf(t);

            for (size_t i = 0; i < n.e.size(); i++) {
                const qp::Entry &e = n.e[i];
                repBound(c.visits[vi], i, n.leaf, hasRep, distanceRepres, e);
                const double slack = n.leaf ? 0.0 : e.radius;
                if (!(std::fabs(distanceRepres - e.distToRep) <= rangeK + slack)) continue;
                double lb, ub;
                const bool pruned =
                    test(t, order, page, c, hasRep, distanceRepres, i, rangeK,
                         weight(n.leaf, e), lb, ub);
                narrow(c.visits[vi], i, lb, ub);
                if (pruned) {
                    if (n.leaf) out.prunedObjects++;
                    else out.prunedSubtrees++;
                    continue;
                }
                distance = ev_.GetDistance(*e.obj, q);
                setExact(c.visits[vi], i, distance, n.leaf, e.radius);
                if (!n.leaf) {
                    out.indexDist++;
                    if (distance <= rangeK + e.radius) {
                        tmp.PageID = e.child;
                        tmp.Radius = e.radius;
                        queue->Add(distance, tmp);
                        ringOfPage[e.child] = lb;
                        parentOf[e.child] = std::make_pair(vi, i);
                    }
                } else {
                    out.leafDist++;
                    if (distance <= rangeK) {
                        out.result.insert(std::upper_bound(out.result.begin(),
                                                           out.result.end(), distance),
                                          distance);
                        if (out.result.size() >= k) {
                            out.result.resize(k);
                            rangeK = out.result.back();
                        }
                    }
                }
            }
            bool stop = false;
            do {
                if (queue->Get(distance, cur)) {
                    if (distance <= rangeK + cur.Radius) {
                        if (ringOfPage[cur.PageID] > rangeK + EPS) {
                            out.droppedAtPop++;
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
            hasRep = true;
        }
        const double finalR = out.result.size() >= k ? out.result.back() : INF;
        finish(c, finalR, out);
        pivotDist = c.pivotDist;
        return out;
    }

    long pivotsRegistered() const { return (long)pivots_.size(); }
    long slotsStored() const {
        long n = 0;
        for (const auto &kv : tables_) n += (long)kv.second.slots.size();
        return n;
    }
    long intervalsStored() const {
        long n = 0;
        for (const auto &kv : tables_)
            for (const Slot &s : kv.second.slots) n += (long)s.lo.size();
        return n;
    }
    long confirmedSlots() const {
        long n = 0;
        for (const auto &kv : tables_)
            for (const Slot &s : kv.second.slots) n += s.confirmed ? 1 : 0;
        return n;
    }
    const Totals &totals() const { return tot_; }

  private:
    struct Ctx {
        TCity *q;
        bool learn;
        std::unordered_map<int, double> dq;  // pivot -> d(q, p)
        std::vector<std::pair<u_int32_t, int> > used;  // (page, slot) consulted
        std::set<std::pair<u_int32_t, int> > usedSet;
        std::vector<Visit> visits;  // visits[0] is the root
        long pivotDist = 0;
        long leafEntriesSeen = 0;
        Ctx(TCity &qq, bool l) : q(&qq), learn(l) {}
    };
    struct Agg {
        double lo, hi;
        int hits;
    };

    Table *tableAt(u_int32_t page) {
        auto it = tables_.find(page);
        if (it == tables_.end() || it->second.slots.empty()) return nullptr;
        return &it->second;
    }

    // Confirmed pivots first, then by decreasing gain.
    static std::vector<int> orderOf(const Table *t) {
        std::vector<int> o;
        if (!t) return o;
        for (size_t i = 0; i < t->slots.size(); i++) o.push_back((int)i);
        std::stable_sort(o.begin(), o.end(), [&](int a, int b) {
            const Slot &x = t->slots[a], &y = t->slots[b];
            if (x.confirmed != y.confirmed) return x.confirmed;
            return x.gain > y.gain;
        });
        return o;
    }

    int enter(Ctx &c, u_int32_t page, const qp::Node &n, bool hasRep, double dRep) {
        Visit v;
        v.page = page;
        v.hasRep = hasRep;
        v.dRep = dRep;
        const size_t m = n.e.size();
        v.lo.assign(m, 0.0);
        v.hi.assign(m, INF);
        v.exact.assign(m, 0);
        v.dist.assign(m, 0.0);
        v.child.assign(m, -1);
        c.visits.push_back(std::move(v));
        if (n.leaf) c.leafEntriesSeen += (long)m;
        if (c.learn && p_.decay > 0.0) {
            auto it = tables_.find(page);
            if (it != tables_.end())
                for (Slot &s : it->second.slots) s.gain *= (1.0 - p_.decay);
        }
        return (int)c.visits.size() - 1;
    }

    // d(q, y) for y in the entry, through the node representative.
    static void repBound(Visit &v, size_t i, bool leaf, bool hasRep, double dRep,
                         const qp::Entry &e) {
        if (!hasRep) return;
        const double a = std::fabs(dRep - e.distToRep), b = dRep + e.distToRep;
        if (leaf) {
            v.lo[i] = a;
            v.hi[i] = b;
        } else {
            v.lo[i] = std::max(0.0, a - e.radius);
            v.hi[i] = b + e.radius;
        }
    }
    static void narrow(Visit &v, size_t i, double lb, double ub) {
        v.lo[i] = std::max(v.lo[i], lb);
        v.hi[i] = std::min(v.hi[i], ub);
    }
    static void setExact(Visit &v, size_t i, double d, bool leaf, double radius) {
        v.exact[i] = 1;
        v.dist[i] = d;
        v.computed++;
        if (leaf) {
            v.lo[i] = v.hi[i] = d;
        } else {
            v.lo[i] = std::max(v.lo[i], std::max(0.0, d - radius));
            v.hi[i] = std::min(v.hi[i], d + radius);
        }
    }

    double weight(bool leaf, const qp::Entry &e) const {
        return leaf ? 1.0 : 1.0 + std::max(0.0, chat_) * (*leafCount_)[e.id];
    }

    double dq(Ctx &c, int pivot) {
        auto it = c.dq.find(pivot);
        if (it != c.dq.end()) return it->second;
        const double d = ev_.GetDistance(*c.q, *pivots_[pivot]);
        c.pivotDist++;
        c.dq[pivot] = d;
        return d;
    }

    // The pivot filter of entry i. lb/ub: bounds on d(q, y) it established.
    bool test(Table *t, const std::vector<int> &order, u_int32_t page, Ctx &c,
              bool hasRep, double dRep, size_t i, double r, double w, double &lb,
              double &ub) {
        lb = 0.0;
        ub = INF;
        if (!t) return false;
        for (int si : order) {
            Slot &s = t->slots[si];
            const double l = s.lo[i], h = s.hi[i];
            if (l <= 0.0 && h >= INF) continue;  // nothing known
            // d(q,p) lies in [A, B] through the node representative. If even
            // the best d in there cannot prune, do not pay for d(q,p).
            if (p_.repSkip && hasRep && s.repDist >= 0.0 &&
                c.dq.find(s.pivot) == c.dq.end()) {
                const double A = std::fabs(dRep - s.repDist), B = dRep + s.repDist;
                if (std::max(l - A, B - h) <= r + EPS) continue;
            }
            const double d = dq(c, s.pivot);
            if (c.usedSet.insert(std::make_pair(page, si)).second)
                c.used.push_back(std::make_pair(page, si));
            lb = std::max(lb, std::max(l - d, d - h));
            ub = std::min(ub, d + h);
            if (lb > r + EPS) {
                if (c.learn) s.gain += w;
                return true;
            }
        }
        return false;
    }

    // Replica of stSlimTree::RangeQuery (see qp::rangeRec) with the tables.
    int rangeNode(Ctx &c, u_int32_t page, bool hasRep, double dRep, double r,
                  qp::Counts &out) {
        const qp::Node &n = s_->node(page);
        out.reads++;
        const int vi = enter(c, page, n, hasRep, dRep);
        Table *t = tableAt(page);
        const std::vector<int> order = orderOf(t);
        for (size_t i = 0; i < n.e.size(); i++) {
            const qp::Entry &e = n.e[i];
            repBound(c.visits[vi], i, n.leaf, hasRep, dRep, e);
            // The public RangeQuery has no parent filter at the root.
            if (hasRep &&
                !(std::fabs(dRep - e.distToRep) <= r + (n.leaf ? 0.0 : e.radius)))
                continue;
            double lb, ub;
            const bool pruned =
                test(t, order, page, c, hasRep, dRep, i, r, weight(n.leaf, e), lb, ub);
            narrow(c.visits[vi], i, lb, ub);
            if (pruned) {
                if (n.leaf) out.prunedObjects++;
                else out.prunedSubtrees++;
                continue;
            }
            const double d = ev_.GetDistance(*e.obj, *c.q);
            setExact(c.visits[vi], i, d, n.leaf, e.radius);
            if (n.leaf) {
                out.leafDist++;
                if (d <= r) out.result.push_back(d);
            } else {
                out.indexDist++;
                if (d <= r + e.radius) {
                    const int ch = rangeNode(c, e.child, true, d, r, out);
                    c.visits[vi].child[i] = ch;
                }
            }
        }
        return vi;
    }

    // Post-order over the visited nodes: tighten the index intervals with the
    // child's own, count the answers below each entry and the false positives.
    Agg finalise(Ctx &c, int vi, double finalR) {
        const qp::Node &n = s_->node(c.visits[vi].page);
        Agg a{INF, 0.0, 0};
        for (size_t i = 0; i < n.e.size(); i++) {
            const int ch = c.visits[vi].child[i];
            if (!n.leaf && ch >= 0) {
                const Agg sub = finalise(c, ch, finalR);
                Visit &v = c.visits[vi];
                v.lo[i] = std::max(v.lo[i], sub.lo);
                v.hi[i] = std::min(v.hi[i], sub.hi);
                if (sub.hits == 0) v.falsePos++;
                a.hits += sub.hits;
            } else if (c.visits[vi].exact[i]) {
                Visit &v = c.visits[vi];
                if (n.leaf && v.dist[i] <= finalR) a.hits++;
                else v.falsePos++;
            }
            const Visit &v = c.visits[vi];
            a.lo = std::min(a.lo, v.lo[i]);
            a.hi = std::max(a.hi, v.hi[i]);
        }
        return a;
    }

    void admit(Table &t, Slot &&s, int cost, double ema, bool first) {
        int trials = 0;
        for (const Slot &x : t.slots) trials += x.confirmed ? 0 : 1;
        const bool full = (int)t.slots.size() >= p_.nodeSlots;
        const bool trialFull = trials >= p_.trialCap;
        if (!full && !trialFull) {
            t.slots.push_back(std::move(s));
            tot_.admitted++;
            return;
        }
        // With the trial cap reached, the victim must be a trial.
        auto eligible = [&](const Slot &x) { return !trialFull || !x.confirmed; };
        int victim = -1;
        for (size_t j = 0; j < t.slots.size(); j++) {
            const Slot &x = t.slots[j];
            if (first || cost < ema) {
                // Cost falling: keep what works, only a useless trial may go.
                if (x.confirmed || x.gain > 0.0) continue;
                if (victim < 0 || x.efficiency() < t.slots[victim].efficiency())
                    victim = (int)j;
            } else if (cost > p_.kappa * ema) {
                // Cost spike: the focus moved, the oldest pivot goes.
                if (!eligible(x)) continue;
                if (victim < 0 || x.birth < t.slots[victim].birth) victim = (int)j;
            } else {
                if (!eligible(x)) continue;
                if (victim < 0 || x.efficiency() < t.slots[victim].efficiency())
                    victim = (int)j;
            }
        }
        if (victim < 0) {
            tot_.refused++;
            return;
        }
        t.slots[victim] = std::move(s);
        tot_.evicted++;
        tot_.admitted++;
    }

    void finish(Ctx &c, double finalR, const qp::Counts &out) {
        if (c.visits.empty()) return;
        finalise(c, 0, finalR);
        if (!c.learn) return;

        // 1. Each d(q,p) is charged once, shared by the slots that needed it.
        std::unordered_map<int, int> share;
        for (const auto &u : c.used) share[tables_[u.first].slots[u.second].pivot]++;
        for (const auto &u : c.used) {
            Slot &s = tables_[u.first].slots[u.second];
            s.gain -= 1.0 / (double)share[s.pivot];
        }
        // 2. Confirmation and demotion.
        for (const Visit &v : c.visits) {
            auto it = tables_.find(v.page);
            if (it == tables_.end()) continue;
            for (Slot &s : it->second.slots) {
                if (!s.confirmed && s.gain > p_.tau) {
                    s.confirmed = true;
                    tot_.confirmations++;
                } else if (s.confirmed && s.gain < 0.0) {
                    s.confirmed = false;
                    tot_.demotions++;
                }
            }
        }
        // 3. The paid column, node by node.
        int pid = -1;
        for (Visit &v : c.visits) {
            Table &t = tables_[v.page];
            const bool first = t.visits == 0;
            const double ema = t.ema;
            if (v.computed >= p_.cMin && v.falsePos >= p_.fMin) {
                if (pid < 0) {
                    pivots_.push_back(c.q);
                    pid = (int)pivots_.size() - 1;
                }
                Slot s;
                s.pivot = pid;
                s.lo = std::move(v.lo);
                s.hi = std::move(v.hi);
                s.known = v.computed;
                s.birth = clock_;
                s.repDist = v.hasRep ? v.dRep : -1.0;
                admit(t, std::move(s), v.computed, ema, first);
            }
            t.ema = first ? (double)v.computed
                          : p_.lambda * v.computed + (1.0 - p_.lambda) * ema;
            t.visits++;
        }
        // 4. c^, the share of a leaf that pays a distance.
        if (c.leafEntriesSeen > 0) {
            const double ratio = (double)out.leafDist / (double)c.leafEntriesSeen;
            chat_ = chat_ < 0.0 ? ratio : p_.lambda * ratio + (1.0 - p_.lambda) * chat_;
        }
        clock_++;
    }

    const qp::Snapshot *s_;
    const std::vector<int> *leafCount_;  // per index entry id
    Params p_;
    TCityDistanceEvaluator ev_;
    std::unordered_map<u_int32_t, Table> tables_;
    std::vector<TCity *> pivots_;  // past query centres, not owned
    double chat_ = -1.0;
    long clock_ = 0;
    Totals tot_;
};

// Leaf entries below every index entry.
static std::vector<int> leafCounts(const qp::Snapshot &s) {
    std::vector<int> out(s.entries(), 0);
    std::function<int(u_int32_t)> walk = [&](u_int32_t page) -> int {
        const qp::Node &n = s.node(page);
        if (n.leaf) return (int)n.e.size();
        int sum = 0;
        for (const qp::Entry &e : n.e) {
            out[e.id] = walk(e.child);
            sum += out[e.id];
        }
        return sum;
    };
    walk(s.root());
    return out;
}

struct EngStats {
    long pivots = 0, slots = 0, intervals = 0, confirmed = 0;
    Totals t;
    static EngStats of(const Engine &e) {
        EngStats s;
        s.pivots = e.pivotsRegistered();
        s.slots = e.slotsStored();
        s.intervals = e.intervalsStored();
        s.confirmed = e.confirmedSlots();
        s.t = e.totals();
        return s;
    }
};

static const char *RESULTS_HEADER =
    "dataset,regime,order,query_type,k,radius,repetition,seed,arm,phase,"
    "node_slots,queries,base_distances,base_reads,distances,index_distances,"
    "leaf_distances,pivot_distances,reads,saving_distances_pct,saving_reads_pct,"
    "pruned_subtrees,pruned_objects,dropped_at_pop,pivots_registered,"
    "slots_stored,intervals_stored,confirmed_slots,admitted,evicted,refused,"
    "real_distances,real_reads,replica_exact,engine_exact,correct";

static const char *ONLINE_HEADER =
    "dataset,regime,order,query_type,repetition,seed,arm,node_slots,block,"
    "first_query,queries,region,base_distances,distances,pivot_distances,"
    "base_reads,reads";

}  // namespace rc

// ---------------------------------------------------------------------------
static void usage() {
    std::cout <<
        "qp_recast - Chapter 5, R0: RECAST-style paid pivot columns per node\n"
        "\n"
        "  --dataset F --queryfile F   cities file and uniform query pool (required)\n"
        "  --regime R                  concentrado | disperso | uniforme\n"
        "  --order O                   mixed | salto (adapt queries region by\n"
        "                              region: the focus jumps)      (mixed)\n"
        "  --query-type T              knn | range | both            (both)\n"
        "  --reps N --rep-offset N --seed N                          (1, 0, 42)\n"
        "  --adapt-queries N --measure-queries N                     (200, 100)\n"
        "  --k N --radius R --page-size N --region-factor F          (10, 0.5, 1024, 3)\n"
        "  --index-dir D               where the temporary index file goes (.)\n"
        "  --node-slots LIST           K, pivots per node; e.g. 2,4,8 (8)\n"
        "  --trial-cap N               trial pivots per node; -1 = K/2 (-1)\n"
        "  --kappa X --lambda X --decay X --tau X                    (4, 0.1, 0.05, 1)\n"
        "  --c-min N --f-min N         admission thresholds          (1, 1)\n"
        "  --block N                   online curve block size       (25)\n"
        "  --no-reference              skip oracle_q, oracle_d, pm_dfar4\n"
        "  --no-rep-skip               always pay d(q,p) before testing a pivot\n"
        "  --out F --online-out F      CSV outputs (appended)\n"
        "  --dataset-label S --no-verify\n";
}

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    Config cfg;
    rc::Params base;
    std::string nodeSlots = "8", order = "mixed";
    int trialCap = -1, block = 25;
    bool reference = true;
    std::string outPath, onlinePath;

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
        else if (a == "--order") order = next("--order");
        else if (a == "--query-type") cfg.queryType = next("--query-type");
        else if (a == "--page-size") cfg.pageSize = (unsigned)std::stoul(next("--page-size"));
        else if (a == "--k") cfg.k = (unsigned)std::stoul(next("--k"));
        else if (a == "--radius") cfg.radius = std::stod(next("--radius"));
        else if (a == "--adapt-queries") cfg.adaptQueries = std::stoi(next("--adapt-queries"));
        else if (a == "--measure-queries") cfg.measureQueries = std::stoi(next("--measure-queries"));
        else if (a == "--reps") cfg.reps = std::stoi(next("--reps"));
        else if (a == "--rep-offset") cfg.repOffset = std::stoi(next("--rep-offset"));
        else if (a == "--seed") cfg.seed = (unsigned)std::stoul(next("--seed"));
        else if (a == "--region-factor") cfg.regionFactor = std::stod(next("--region-factor"));
        else if (a == "--no-verify") cfg.verify = false;
        else if (a == "--node-slots") nodeSlots = next("--node-slots");
        else if (a == "--trial-cap") trialCap = std::stoi(next("--trial-cap"));
        else if (a == "--kappa") base.kappa = std::stod(next("--kappa"));
        else if (a == "--lambda") base.lambda = std::stod(next("--lambda"));
        else if (a == "--decay") base.decay = std::stod(next("--decay"));
        else if (a == "--tau") base.tau = std::stod(next("--tau"));
        else if (a == "--c-min") base.cMin = std::stoi(next("--c-min"));
        else if (a == "--f-min") base.fMin = std::stoi(next("--f-min"));
        else if (a == "--block") block = std::stoi(next("--block"));
        else if (a == "--no-reference") reference = false;
        else if (a == "--no-rep-skip") base.repSkip = false;
        else if (a == "--out") outPath = next("--out");
        else if (a == "--online-out") onlinePath = next("--online-out");
        else {
            std::cerr << "ERROR: unknown option " << a << "\n";
            return 2;
        }
    }
    if (cfg.dataset.empty() || cfg.queryFile.empty()) {
        std::cerr << "ERROR: --dataset and --queryfile are required\n";
        return 2;
    }
    if (cfg.regime != "concentrado" && cfg.regime != "disperso" &&
        cfg.regime != "uniforme") {
        std::cerr << "ERROR: unknown regime: " << cfg.regime << "\n";
        return 2;
    }
    if (order != "mixed" && order != "salto") {
        std::cerr << "ERROR: --order must be mixed or salto\n";
        return 2;
    }
    if (order == "salto" && cfg.regime == "uniforme") {
        std::cerr << "ERROR: --order salto needs regions (concentrado or disperso)\n";
        return 2;
    }
    std::vector<int> Ks;
    {
        std::stringstream ss(nodeSlots);
        std::string tok;
        while (std::getline(ss, tok, ','))
            if (!tok.empty()) Ks.push_back(std::stoi(tok));
    }
    if (Ks.empty() || block < 1) {
        std::cerr << "ERROR: --node-slots needs at least one value, --block >= 1\n";
        return 2;
    }
    for (int K : Ks)
        if (K < 1 || K > 64) {
            std::cerr << "ERROR: --node-slots values must lie in [1, 64]\n";
            return 2;
        }

    std::vector<TCity *> objects = ch4::loadCities(cfg.dataset);
    std::vector<TCity *> pool = ch4::loadCities(cfg.queryFile);
    std::vector<std::string> queryTypes;
    if (cfg.queryType == "both") {
        queryTypes.push_back("knn");
        queryTypes.push_back("range");
    } else {
        queryTypes.push_back(cfg.queryType);
    }

    std::ofstream outFile, onlineFile;
    ch4::openCsv(outFile, outPath, rc::RESULTS_HEADER);
    ch4::openCsv(onlineFile, onlinePath, rc::ONLINE_HEADER);

    // Separate evaluators, so no bookkeeping distance lands in a measured count.
    TCityDistanceEvaluator genEval, verifyEval, pivotEval, simEval, oracleEval;
    int failures = 0;

    for (int r = 0; r < cfg.reps; r++) {
        const int rep = cfg.repOffset + r;
        const unsigned int repSeed = cfg.seed + (unsigned int)rep;

        // Exactly the workload and the insertion order of chapter4.cpp.
        Workload w = makeWorkload(cfg, genEval, objects, pool, repSeed);
        if (order == "salto") {
            // Same queries, region by region: the focus jumps |regions|-1 times.
            std::vector<size_t> idx(w.adapt.size());
            std::iota(idx.begin(), idx.end(), (size_t)0);
            std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
                return w.adaptRegion[a] < w.adaptRegion[b];
            });
            std::vector<TCity *> a2;
            std::vector<int> g2;
            for (size_t i : idx) {
                a2.push_back(w.adapt[i]);
                g2.push_back(w.adaptRegion[i]);
            }
            w.adapt.swap(a2);
            w.adaptRegion.swap(g2);
        }
        std::vector<TCity *> insertOrder(objects);
        {
            std::mt19937 orderRng(repSeed + 1000003u);
            std::shuffle(insertOrder.begin(), insertOrder.end(), orderRng);
        }
        // Control centres: the same random data sample as F0's d_nearest.
        std::vector<TCity *> dataPool(objects);
        {
            std::mt19937 poolRng(repSeed + 5000011u);
            std::shuffle(dataPool.begin(), dataPool.end(), poolRng);
            dataPool.resize(std::min(dataPool.size(), w.adapt.size()));
        }
        std::cout << "regime " << cfg.regime << " (" << order << ") | rep " << rep
                  << " seed " << repSeed << " | adapt " << w.adapt.size()
                  << " measure " << w.measure.size() << "\n";

        const std::string idxPath =
            cfg.indexDir + "/qp_recast_" + cfg.regime + "_" + order + ".dat";
        std::error_code ec;
        std::filesystem::remove(idxPath, ec);
        stPlainDiskPageManager *pm =
            new stPlainDiskPageManager(idxPath.c_str(), cfg.pageSize);
        mySlimTree *tree = new mySlimTree(pm);
        for (TCity *o : insertOrder) tree->Add(o);

        qp::Snapshot snap(pm, tree->GetRootPageID());
        if (snap.leafEntries() != (long)objects.size()) {
            std::cerr << "ERROR: snapshot has " << snap.leafEntries()
                      << " leaf entries, expected " << objects.size() << "\n";
            return 1;
        }
        const std::vector<int> leafCount = rc::leafCounts(snap);

        // References (measure only): the F0 oracle, its control, global pivots.
        qp::ColumnCache columns(snap, pivotEval);
        std::vector<TCity *> nearQ, nearD;
        std::vector<TCity *> dFar4;
        if (reference) {
            auto nearestOf = [&](const std::vector<TCity *> &cand) {
                std::vector<TCity *> out;
                for (TCity *m : w.measure) {
                    TCity *best = cand.empty() ? nullptr : cand[0];
                    double bestDist = INFINITY;
                    for (TCity *a : cand) {
                        const double d = oracleEval.GetDistance(*m, *a);
                        if (d < bestDist) {
                            bestDist = d;
                            best = a;
                        }
                    }
                    out.push_back(best);
                }
                return out;
            };
            nearQ = nearestOf(w.adapt);
            nearD = nearestOf(dataPool);
            dFar4 = qp::farthestFirstData(objects, 4, repSeed + 2000003u, pivotEval);
        }

        for (const std::string &qtype : queryTypes) {
            const bool knn = (qtype == "knn");
            auto brute = [&](const std::vector<TCity *> &qs) {
                std::vector<std::vector<double> > out;
                if (!cfg.verify) return out;
                for (TCity *q : qs)
                    out.push_back(knn ? ch4::bruteKnn(verifyEval, objects, q, cfg.k)
                                      : ch4::bruteRange(verifyEval, objects, q, cfg.radius));
                return out;
            };
            const auto wantAdapt = brute(w.adapt);
            const auto wantMeasure = brute(w.measure);
            const auto wantData = brute(dataPool);
            auto ok = [&](const qp::Counts &c, const std::vector<std::vector<double> > &want,
                          size_t i) {
                return !cfg.verify || ch4::sameDistances(c.result, want[i]);
            };
            auto replica = [&](TCity *q, const qp::PivotView &pv) {
                return knn ? qp::simKnn(snap, *q, cfg.k, pv, simEval)
                           : qp::simRange(snap, *q, cfg.radius, pv, simEval);
            };
            auto runEngine = [&](rc::Engine &e, TCity *q, bool learn, long &pd) {
                return knn ? e.knn(*q, cfg.k, learn, pd) : e.range(*q, cfg.radius, learn, pd);
            };

            // ---- the Slim-tree: real tree (measure) and replica (all) ---------
            qp::Sums baseAdapt, baseMeasure, baseData, real;
            std::vector<qp::Counts> baseAdaptQ, baseMeasureQ;
            const qp::PivotView none;
            bool replicaExact = true;
            for (size_t i = 0; i < w.adapt.size(); i++) {
                const qp::Counts c = replica(w.adapt[i], none);
                baseAdapt.add(c, 0, 0);
                if (!ok(c, wantAdapt, i)) baseAdapt.correct = false;
                baseAdaptQ.push_back(c);
            }
            for (size_t i = 0; i < dataPool.size(); i++) {
                const qp::Counts c = replica(dataPool[i], none);
                baseData.add(c, 0, 0);
                if (!ok(c, wantData, i)) baseData.correct = false;
            }
            for (size_t i = 0; i < w.measure.size(); i++) {
                TCity *q = w.measure[i];
                const long d0 = (long)tree->GetMetricEvaluator()->GetDistanceCount();
                const long r0 = (long)pm->GetReadCount();
                myResult *res = knn ? tree->NearestQuery(q, cfg.k)
                                    : tree->RangeQuery(q, cfg.radius);
                const long realDist =
                    (long)tree->GetMetricEvaluator()->GetDistanceCount() - d0;
                const long realReads = (long)pm->GetReadCount() - r0;
                real.queries++;
                real.leafDist += realDist;
                real.reads += realReads;
                if (cfg.verify && !ch4::sameDistances(ch4::resultDistances(res), wantMeasure[i]))
                    real.correct = false;
                delete res;

                const qp::Counts c = replica(q, none);
                baseMeasure.add(c, 0, 0);
                if (!ok(c, wantMeasure, i)) baseMeasure.correct = false;
                baseMeasureQ.push_back(c);
                if (c.indexDist + c.leafDist != realDist || c.reads != realReads) {
                    if (replicaExact)
                        std::cerr << "WARNING: replica differs from the tree on query " << i
                                  << " (" << qtype << ")\n";
                    replicaExact = false;
                }
            }

            // ---- the engine with empty tables must BE the replica ---------------
            bool engineExact = true;
            {
                rc::Params p = base;
                p.nodeSlots = Ks[0];
                p.trialCap = trialCap >= 0 ? trialCap : std::max(1, Ks[0] / 2);
                rc::Engine empty(snap, leafCount, p);
                for (size_t i = 0; i < w.measure.size(); i++) {
                    long pd = 0;
                    const qp::Counts c = runEngine(empty, w.measure[i], false, pd);
                    const qp::Counts &b = baseMeasureQ[i];
                    if (c.indexDist != b.indexDist || c.leafDist != b.leafDist ||
                        c.reads != b.reads || pd != 0 || c.result != b.result) {
                        if (engineExact)
                            std::cerr << "WARNING: empty engine differs from the replica on "
                                      << "query " << i << " (" << qtype << ")\n";
                        engineExact = false;
                    }
                }
            }

            const double nA = baseAdapt.queries ? (double)baseAdapt.queries : 1.0;
            const double nM = baseMeasure.queries ? (double)baseMeasure.queries : 1.0;
            const double nD = baseData.queries ? (double)baseData.queries : 1.0;
            auto row = [&](const std::string &arm, const std::string &phase, int K,
                           const qp::Sums &x, const qp::Sums &b, const rc::EngStats *st) {
                const double nq = x.queries ? (double)x.queries : 1.0;
                const double nb = b.queries ? (double)b.queries : 1.0;
                const double bd = b.distances() / nb, br = b.reads / nb;
                const double dist = x.distances() / nq, reads = x.reads / nq;
                const bool good = x.correct && b.correct && real.correct;
                if (!good) failures++;
                std::cout << "    " << std::left << std::setw(12) << arm << std::right
                          << " " << phase << " K=" << K << ": " << std::fixed
                          << std::setprecision(1) << dist << " dist ("
                          << 100.0 * (bd - dist) / bd << "%), " << reads << " reads ("
                          << 100.0 * (br - reads) / br << "%), pivots "
                          << x.pivotDist / nq << std::defaultfloat
                          << std::setprecision(6) << (good ? "" : "  WRONG") << "\n";
                if (!outFile.is_open()) return;
                outFile << cfg.datasetLabel << ',' << cfg.regime << ',' << order << ','
                        << qtype << ',' << cfg.k << ',' << cfg.radius << ',' << rep << ','
                        << repSeed << ',' << arm << ',' << phase << ',' << K << ','
                        << x.queries << ',' << bd << ',' << br << ',' << dist << ','
                        << x.indexDist / nq << ',' << x.leafDist / nq << ','
                        << x.pivotDist / nq << ',' << reads << ','
                        << 100.0 * (bd - dist) / bd << ',' << 100.0 * (br - reads) / br
                        << ',' << x.prunedSubtrees / nq << ',' << x.prunedObjects / nq
                        << ',' << x.droppedAtPop / nq << ',';
                if (st)
                    outFile << st->pivots << ',' << st->slots << ',' << st->intervals << ','
                            << st->confirmed << ',' << st->t.admitted << ','
                            << st->t.evicted << ',' << st->t.refused;
                else
                    outFile << ",,,,,,";
                outFile << ',' << real.leafDist / nM << ',' << real.reads / nM << ','
                        << (replicaExact ? 1 : 0) << ',' << (engineExact ? 1 : 0) << ','
                        << (good ? 1 : 0) << "\n";
            };
            (void)nA;
            (void)nD;

            std::cout << "  " << qtype << ": base " << baseMeasure.distances() / nM
                      << " dist, " << baseMeasure.reads / nM << " reads | replica "
                      << (replicaExact ? "exact" : "DIFFERS") << " | empty engine "
                      << (engineExact ? "exact" : "DIFFERS") << "\n";
            if (!replicaExact) failures++;
            if (!engineExact) failures++;
            row("none", "adapt", 0, baseAdapt, baseAdapt, nullptr);
            row("none", "measure", 0, baseMeasure, baseMeasure, nullptr);

            for (int K : Ks) {
                rc::Params p = base;
                p.nodeSlots = K;
                p.trialCap = trialCap >= 0 ? trialCap : std::max(1, K / 2);

                // ---- recast: the adapt queries build the tables online ---------
                rc::Engine eng(snap, leafCount, p);
                qp::Sums adapt;
                const int nBlocks = ((int)w.adapt.size() + block - 1) / block;
                std::vector<qp::Sums> blkBase(nBlocks), blkRun(nBlocks);
                for (size_t i = 0; i < w.adapt.size(); i++) {
                    long pd = 0;
                    const qp::Counts c = runEngine(eng, w.adapt[i], true, pd);
                    adapt.add(c, (double)pd, 0);
                    if (!ok(c, wantAdapt, i)) adapt.correct = false;
                    blkBase[i / block].add(baseAdaptQ[i], 0, 0);
                    blkRun[i / block].add(c, (double)pd, 0);
                }
                const rc::EngStats learned = rc::EngStats::of(eng);
                row("recast", "adapt", K, adapt, baseAdapt, &learned);
                if (onlineFile.is_open())
                    for (int b = 0; b < nBlocks; b++) {
                        const double nq = (double)blkRun[b].queries;
                        onlineFile << cfg.datasetLabel << ',' << cfg.regime << ',' << order
                                   << ',' << qtype << ',' << rep << ',' << repSeed
                                   << ",recast," << K << ',' << b << ',' << b * block << ','
                                   << blkRun[b].queries << ',' << w.adaptRegion[b * block]
                                   << ',' << blkBase[b].distances() / nq << ','
                                   << blkRun[b].distances() / nq << ','
                                   << blkRun[b].pivotDist / nq << ','
                                   << blkBase[b].reads / nq << ',' << blkRun[b].reads / nq
                                   << "\n";
                    }

                // ---- frozen tables for the measured queries ---------------------
                {
                    rc::Engine frozen(eng);
                    qp::Sums m;
                    for (size_t i = 0; i < w.measure.size(); i++) {
                        long pd = 0;
                        const qp::Counts c = runEngine(frozen, w.measure[i], false, pd);
                        m.add(c, (double)pd, 0);
                        if (!ok(c, wantMeasure, i)) m.correct = false;
                    }
                    row("recast", "measure", K, m, baseMeasure, &learned);
                }
                // ---- the same tables, still learning ----------------------------
                {
                    rc::Engine live(eng);
                    qp::Sums m;
                    for (size_t i = 0; i < w.measure.size(); i++) {
                        long pd = 0;
                        const qp::Counts c = runEngine(live, w.measure[i], true, pd);
                        m.add(c, (double)pd, 0);
                        if (!ok(c, wantMeasure, i)) m.correct = false;
                    }
                    const rc::EngStats st = rc::EngStats::of(live);
                    row("recast_live", "measure", K, m, baseMeasure, &st);
                }
                // ---- control: the same number of "queries" on random cities ----
                {
                    rc::Engine data(snap, leafCount, p);
                    qp::Sums over;
                    for (size_t i = 0; i < dataPool.size(); i++) {
                        long pd = 0;
                        const qp::Counts c = runEngine(data, dataPool[i], true, pd);
                        over.add(c, (double)pd, 0);
                        if (!ok(c, wantData, i)) over.correct = false;
                    }
                    const rc::EngStats st = rc::EngStats::of(data);
                    row("recast_data", "adapt", K, over, baseData, &st);
                    qp::Sums m;
                    for (size_t i = 0; i < w.measure.size(); i++) {
                        long pd = 0;
                        const qp::Counts c = runEngine(data, w.measure[i], false, pd);
                        m.add(c, (double)pd, 0);
                        if (!ok(c, wantMeasure, i)) m.correct = false;
                    }
                    row("recast_data", "measure", K, m, baseMeasure, &st);
                }
            }

            // ---- references -------------------------------------------------
            if (reference) {
                qp::Sums oq, od, pf;
                for (size_t i = 0; i < w.measure.size(); i++) {
                    TCity *q = w.measure[i];
                    auto one = [&](const std::vector<TCity *> &pivots, qp::Sums &s) {
                        qp::PivotView pv;
                        for (TCity *p : pivots) {
                            pv.cols.push_back(columns.get(p));
                            pv.dq.push_back(simEval.GetDistance(*q, *p));
                        }
                        const qp::Counts c = replica(q, pv);
                        s.add(c, (double)pivots.size(), 0);
                        if (!ok(c, wantMeasure, i)) s.correct = false;
                    };
                    one(std::vector<TCity *>(1, nearQ[i]), oq);
                    one(std::vector<TCity *>(1, nearD[i]), od);
                    one(dFar4, pf);
                }
                row("oracle_q", "measure", 1, oq, baseMeasure, nullptr);
                row("oracle_d", "measure", 1, od, baseMeasure, nullptr);
                row("pm_dfar4", "measure", 4, pf, baseMeasure, nullptr);
            }
        }

        delete tree;
        delete pm;
        std::filesystem::remove(idxPath, ec);
    }

    ch4::deleteAll(objects);
    ch4::deleteAll(pool);
    if (failures) {
        std::cerr << "ERROR: " << failures
                  << " batch(es) failed verification, the replica or the empty-engine check\n";
        return 1;
    }
    return 0;
}
