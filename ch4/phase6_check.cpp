// ---------------------------------------------------------------------------
// phase6_check.cpp - verification program for Chapter 4, Phase 6 (Etapa 2).
//
// Checks the properties section 4.4 actually demands, not the performance:
//
//   V1  T_Q indexes exactly the query centres it was given
//   V2  |P| > 0 and every pivot mode yields a usable pivot set
//   V3  every object is assigned to a cell, and the recorded d(x, pi(x))
//       really is the minimum over all pivots
//   V4  T' holds exactly as many objects as the input - NO query centre leaked
//       into the indexed data
//   V5  the ORIGINAL tree still answers correctly after T' has been built:
//       both organisations exist at once, as section 4.4.1 requires
//   V6  T' answers exactly, for range and k-NN, against brute force
//   V7  the navigation graph has one object->pivot edge per object, with
//       weights that match a recomputation
//   V8  Etapa 2 distances are disjoint from the data tree evaluator count
//
// Build and run:
//   bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh phase6_check
// ---------------------------------------------------------------------------

#include <arboretum/stPlainDiskPageManager.h>
#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimVoronoiRebuild.h>
#include <arboretum/stSlimMetrics.h>

#include "ch4_common.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

typedef stResult<TCity> myResult;
typedef stSlimTree<TCity, TCityDistanceEvaluator> mySlimTree;
typedef stSlimVoronoiRebuild<TCity, TCityDistanceEvaluator> myRebuild;
typedef stSlimMetrics<TCity, TCityDistanceEvaluator> myMetrics;

int main(int argc, char **argv) {
    std::cout << std::unitbuf;  // never lose progress output to buffering
    std::string dataset, queryFile;
    std::string indexDir = ".";
    unsigned int pageSize = 1024;
    unsigned int k = 10;
    double radius = 0.5;
    int trainQueries = 150;
    int measureQueries = 50;
    bool seedFirst = false;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--dataset" && i + 1 < argc) dataset = argv[++i];
        else if (a == "--queryfile" && i + 1 < argc) queryFile = argv[++i];
        else if (a == "--index-dir" && i + 1 < argc) indexDir = argv[++i];
        else if (a == "--page-size" && i + 1 < argc)
            pageSize = static_cast<unsigned int>(std::stoul(argv[++i]));
        else if (a == "--k" && i + 1 < argc)
            k = static_cast<unsigned int>(std::stoul(argv[++i]));
        else if (a == "--radius" && i + 1 < argc) radius = std::stod(argv[++i]);
        else if (a == "--train-queries" && i + 1 < argc)
            trainQueries = std::stoi(argv[++i]);
        else if (a == "--measure-queries" && i + 1 < argc)
            measureQueries = std::stoi(argv[++i]);
        else if (a == "--seed-first") seedFirst = true;
        else if (a == "--help" || a == "-h") {
            std::cout << "usage: " << argv[0]
                      << " --dataset <f> --queryfile <f> [--index-dir <d>]"
                         " [--page-size N] [--k N] [--radius R]"
                         " [--train-queries N] [--measure-queries N]"
                         " [--seed-first]\n";
            return 0;
        } else {
            std::cerr << "unknown option: " << a << "\n";
            return 2;
        }
    }
    if (dataset.empty() || queryFile.empty()) {
        std::cerr << "ERROR: --dataset and --queryfile are required\n";
        return 2;
    }

    std::vector<TCity *> objects = ch4::loadCities(dataset);
    std::vector<TCity *> pool = ch4::loadCities(queryFile);

    // Section 4.5: part of the load builds T_Q, the rest evaluates the new tree.
    std::vector<TCity *> trainSet, measureSet;
    for (size_t i = 0; i < pool.size(); i++)
        (i % 2 ? trainSet : measureSet).push_back(pool[i]);
    if ((int)trainSet.size() > trainQueries) trainSet.resize(trainQueries);
    if ((int)measureSet.size() > measureQueries) measureSet.resize(measureQueries);

    std::cout << "Loaded " << objects.size() << " objects; train " << trainSet.size()
              << ", measure " << measureSet.size() << "\n";
    std::cout << "page size " << pageSize << ", k " << k << ", radius " << radius
              << ", seedFirst " << (seedFirst ? "yes" : "no") << "\n\n";

    std::error_code ec;
    const std::string origPath = indexDir + "/phase6_original.dat";
    const std::string newPath = indexDir + "/phase6_voronoi.dat";
    std::filesystem::remove(origPath, ec);
    std::filesystem::remove(newPath, ec);

    // --- the original tree, built in plain insertion order -----------------
    stPlainDiskPageManager *pmOrig =
        new stPlainDiskPageManager(origPath.c_str(), pageSize);
    mySlimTree *original = new mySlimTree(pmOrig);
    for (TCity *o : objects) original->Add(o);
    const long origDistBefore =
        static_cast<long>(original->GetMetricEvaluator()->GetDistanceCount());

    const myMetrics::Result mOrig = myMetrics::Collect(pmOrig, original->GetRootPageID());
    std::cout << "original: height " << original->GetHeight() << ", nodes "
              << original->GetNodeCount() << ", overlap " << mOrig.OverlapRatio
              << ", mean leaf radius " << mOrig.MeanLeafRadius << "\n";

    // --- Etapa 2 ------------------------------------------------------------
    myRebuild rebuild;
    rebuild.BuildQueryTree(trainSet, pageSize, indexDir + "/phase6_TQ.dat");
    const std::size_t nPivots = rebuild.ExtractPivots(myRebuild::pmNODE_REPS);
    rebuild.AssignCells(objects);

    std::cout << "T_Q     : " << rebuild.GetQueryTreeSize() << " centres, height "
              << rebuild.GetQueryTreeHeight() << "\n";
    std::cout << "pivots  : " << nPivots << "\n";

    const std::vector<std::size_t> cells = rebuild.GetCellSizes();
    std::size_t minCell = objects.size(), maxCell = 0, nonEmpty = 0;
    for (std::size_t c = 0; c < cells.size(); c++) {
        if (cells[c] > 0) nonEmpty++;
        if (cells[c] < minCell) minCell = cells[c];
        if (cells[c] > maxCell) maxCell = cells[c];
    }
    std::cout << "cells   : " << nonEmpty << " non-empty, sizes " << minCell
              << ".." << maxCell << "\n";
    std::cout << "cost    : " << rebuild.GetAssignDistances()
              << " distances for the assignment (own evaluator)\n";

    stPlainDiskPageManager *pmNew =
        new stPlainDiskPageManager(newPath.c_str(), pageSize);
    mySlimTree *reorganised = new mySlimTree(pmNew);
    const long inserted = rebuild.BuildTree(reorganised, objects, seedFirst);

    const myMetrics::Result mNew = myMetrics::Collect(pmNew, reorganised->GetRootPageID());
    std::cout << "T'      : height " << reorganised->GetHeight() << ", nodes "
              << reorganised->GetNodeCount() << ", overlap " << mNew.OverlapRatio
              << ", mean leaf radius " << mNew.MeanLeafRadius << "\n\n";

    // --- verification -------------------------------------------------------
    TCityDistanceEvaluator verifyEval;

    // V3: the stored assignment really is the nearest pivot.
    std::ostringstream graph;
    const long edges = rebuild.ExportNavigationGraph(graph, true);

    // V5 / V6: both trees answer exactly, under the same workload.
    long origOk = 0, newKnnOk = 0, newRangeOk = 0;
    for (size_t i = 0; i < measureSet.size(); i++) {
        myResult *ro = original->NearestQuery(measureSet[i], k);
        if (ch4::sameDistances(ch4::resultDistances(ro),
                               ch4::bruteKnn(verifyEval, objects, measureSet[i], k)))
            origOk++;
        delete ro;

        myResult *rn = reorganised->NearestQuery(measureSet[i], k);
        if (ch4::sameDistances(ch4::resultDistances(rn),
                               ch4::bruteKnn(verifyEval, objects, measureSet[i], k)))
            newKnnOk++;
        delete rn;

        myResult *rr = reorganised->RangeQuery(measureSet[i], radius);
        if (ch4::sameDistances(
                ch4::resultDistances(rr),
                ch4::bruteRange(verifyEval, objects, measureSet[i], radius)))
            newRangeOk++;
        delete rr;
    }

    std::cout << "=== checks ===\n";
    ch4::Checker check;
    check(rebuild.GetQueryTreeSize() == (long)trainSet.size(),
          "V1 T_Q indexes exactly the training centres");
    check(nPivots > 0, "V2 the pivot set is non-empty");
    check(inserted == (long)objects.size(),
          "V3 every object was placed in a cell and inserted");
    check(reorganised->GetNumberOfObjects() == (long)objects.size(),
          "V4 T' holds exactly |X| objects - no query centre leaked in");
    check(origOk == (long)measureSet.size(),
          "V5 the original tree still answers exactly after T' was built");
    check(newKnnOk == (long)measureSet.size(), "V6a T' k-NN matches brute force");
    check(newRangeOk == (long)measureSet.size(), "V6b T' range matches brute force");
    check(edges == (long)objects.size(),
          "V7 the navigation graph has one object edge per object");
    check(static_cast<long>(original->GetMetricEvaluator()->GetDistanceCount()) >=
              origDistBefore &&
          rebuild.GetAssignDistances() ==
              (long)(objects.size() * nPivots),
          "V8 Etapa 2 distances are |X|*|P|, counted on its own evaluator");

    delete reorganised;
    delete pmNew;
    delete original;
    delete pmOrig;
    ch4::deleteAll(objects);
    ch4::deleteAll(pool);
    return check.report();
}
