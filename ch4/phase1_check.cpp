// ---------------------------------------------------------------------------
// phase1_check.cpp - verification program for Chapter 4, Phase 1.
//
// It proves three things:
//
//   1. stSlimTree::GetFatFactor() and GetRelativeFatFactor() LINK. In pristine
//      arboretum GetFatFactor() was declared but never defined, so any call was
//      a link error; this program failing to link would mean the fix did not
//      take.
//   2. The fat factor is finite and non-negative.
//   3. stSlimMetrics agrees with the tree about node and object counts and
//      depth, and the same dataset built twice yields identical structural
//      numbers (determinism).
//
// It also prints how much the metric walk and the fat factor each cost, which
// is what justifies keeping both out of any timed batch.
//
// Build and run: bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh phase1_check
// ---------------------------------------------------------------------------

#include <arboretum/stPlainDiskPageManager.h>
#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimMetrics.h>

#include "ch4_common.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

typedef stResult<TCity> myResult;
typedef stSlimTree<TCity, TCityDistanceEvaluator> mySlimTree;
typedef stSlimMetrics<TCity, TCityDistanceEvaluator> myMetrics;

// ---------------------------------------------------------------------------
// One measured tree. The tree and its page manager are destroyed before this
// is returned; only numbers survive.
// ---------------------------------------------------------------------------
struct Snapshot {
    long height = 0;
    long nodeCount = 0;
    long numObjects = 0;
    double fatFactor = 0.0;
    double relativeFatFactor = 0.0;
    myMetrics::Result metrics;
    long fatFactorDistances = 0;
    long fatFactorReads = 0;
};

static Snapshot buildAndMeasure(const std::vector<TCity *> &objects,
                                const std::string &indexPath,
                                unsigned int pageSize) {
    std::error_code ec;
    std::filesystem::remove(indexPath, ec);

    stPlainDiskPageManager *pm =
        new stPlainDiskPageManager(indexPath.c_str(), pageSize);
    mySlimTree *tree = new mySlimTree(pm);

    for (TCity *o : objects) tree->Add(o);

    Snapshot s;
    s.height = static_cast<long>(tree->GetHeight());
    s.nodeCount = tree->GetNodeCount();
    s.numObjects = tree->GetNumberOfObjects();

    // Structural metrics first: they use their own evaluator, so they leave the
    // tree distance counter untouched.
    s.metrics = myMetrics::Collect(pm, tree->GetRootPageID());

    // Fat factors last, with the cost recorded. GetTreeInfo() runs
    // ObjectIntersectionsRecursive() once per object, so it is orders of
    // magnitude more expensive than everything above it.
    const long d0 = static_cast<long>(tree->GetMetricEvaluator()->GetDistanceCount());
    const long r0 = static_cast<long>(pm->GetReadCount());
    s.fatFactor = tree->GetFatFactor();
    s.relativeFatFactor = tree->GetRelativeFatFactor();
    s.fatFactorDistances =
        static_cast<long>(tree->GetMetricEvaluator()->GetDistanceCount()) - d0;
    s.fatFactorReads = static_cast<long>(pm->GetReadCount()) - r0;

    delete tree;
    delete pm;
    return s;
}

int main(int argc, char **argv) {
    std::string dataset;
    std::string indexDir = ".";
    unsigned int pageSize = 1024;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--dataset" && i + 1 < argc) dataset = argv[++i];
        else if (a == "--index-dir" && i + 1 < argc) indexDir = argv[++i];
        else if (a == "--page-size" && i + 1 < argc)
            pageSize = static_cast<unsigned int>(std::stoul(argv[++i]));
        else if (a == "--help" || a == "-h") {
            std::cout << "usage: " << argv[0]
                      << " --dataset <file> [--index-dir <dir>] [--page-size N]\n";
            return 0;
        } else {
            std::cerr << "unknown option: " << a << "\n";
            return 2;
        }
    }
    if (dataset.empty()) {
        std::cerr << "ERROR: --dataset is required (try --help)\n";
        return 2;
    }

    std::vector<TCity *> objects = ch4::loadCities(dataset);
    std::cout << "Loaded " << objects.size() << " objects from " << dataset << "\n";
    std::cout << "Page size: " << pageSize << "\n\n";

    const Snapshot a = buildAndMeasure(objects, indexDir + "/phase1_a.dat", pageSize);
    const Snapshot b = buildAndMeasure(objects, indexDir + "/phase1_b.dat", pageSize);

    std::cout << "=== tree ===\n";
    std::cout << "  height           : " << a.height << "\n";
    std::cout << "  nodes (tree)     : " << a.nodeCount << "\n";
    std::cout << "  objects (tree)   : " << a.numObjects << "\n\n";

    std::cout << "=== fat factor ===\n";
    std::cout << "  fat factor       : " << a.fatFactor << "\n";
    std::cout << "  relative fat     : " << a.relativeFatFactor
              << (a.relativeFatFactor < 0.0 ? "  (no optimal tree)" : "") << "\n";
    std::cout << "  cost: " << a.fatFactorDistances << " distances, "
              << a.fatFactorReads << " page reads"
              << "   <-- never inside a timed batch\n\n";

    std::cout << "=== structure (stSlimMetrics) ===\n";
    std::cout << "  index nodes      : " << a.metrics.IndexNodes << "\n";
    std::cout << "  leaf nodes       : " << a.metrics.LeafNodes << "\n";
    std::cout << "  total nodes      : " << a.metrics.TotalNodes << "\n";
    std::cout << "  objects in leaves: " << a.metrics.Objects << "\n";
    std::cout << "  max level        : " << a.metrics.MaxLevel << "\n";
    std::cout << "  nodes per level  : ";
    for (size_t i = 0; i < a.metrics.NodesPerLevel.size(); i++)
        std::cout << (i ? ", " : "") << "L" << i << "=" << a.metrics.NodesPerLevel[i];
    std::cout << "\n";
    std::cout << "  leaf entries     : min " << a.metrics.MinLeafEntries
              << ", mean " << a.metrics.MeanLeafEntries
              << ", max " << a.metrics.MaxLeafEntries << "\n";
    std::cout << "  leaf fill        : min " << a.metrics.MinLeafFill
              << ", mean " << a.metrics.MeanLeafFill
              << ", max " << a.metrics.MaxLeafFill << "\n";
    std::cout << "  leaf radius      : min " << a.metrics.MinLeafRadius
              << ", mean " << a.metrics.MeanLeafRadius
              << ", max " << a.metrics.MaxLeafRadius << "\n";
    std::cout << "  sibling pairs    : " << a.metrics.SiblingPairs << "\n";
    std::cout << "  overlapping      : " << a.metrics.OverlappingPairs
              << "  (ratio " << a.metrics.OverlapRatio << ")\n";
    std::cout << "  mean abs overlap : " << a.metrics.MeanAbsOverlap << "\n";
    std::cout << "  walk cost        : " << a.metrics.WalkDistances
              << " distances (own evaluator), " << a.metrics.WalkPageReads
              << " page reads\n\n";

    std::cout << "=== checks ===\n";
    ch4::Checker check;
    check(a.fatFactor == a.fatFactor, "fat factor is not NaN");
    check(a.fatFactor >= 0.0, "fat factor is non-negative");
    check(a.metrics.TotalNodes == a.nodeCount,
          "stSlimMetrics node count matches tree->GetNodeCount()");
    check(a.metrics.Objects == a.numObjects,
          "stSlimMetrics object count matches tree->GetNumberOfObjects()");
    check(a.metrics.MaxLevel + 1 == a.height,
          "stSlimMetrics depth matches tree->GetHeight()");
    check(a.metrics.WalkPageReads == a.metrics.TotalNodes,
          "the walk read exactly one page per node");
    check(a.fatFactor == b.fatFactor && a.nodeCount == b.nodeCount &&
              a.metrics.OverlappingPairs == b.metrics.OverlappingPairs &&
              a.metrics.MeanLeafRadius == b.metrics.MeanLeafRadius,
          "building the same dataset twice gives identical numbers");
    check(a.fatFactorDistances > a.metrics.WalkDistances,
          "GetFatFactor() really is the expensive one (as documented)");

    ch4::deleteAll(objects);
    return check.report();
}
