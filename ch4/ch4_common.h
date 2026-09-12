/**
* @file
*
* Helpers shared by the Chapter 4 drivers.
*
* The dataset loader and the brute-force verifiers are COPIED from bench.cpp
* rather than factored out of it. That duplication is deliberate: bench.cpp
* produces the Chapter 3 numbers, which must stay reproducible from commit
* cf6fd9d, and refactoring it would risk perturbing them for no Chapter 4
* benefit. Keep the two copies behaviourally identical - in particular the
* parser, which has to cope with a Latin-1, CRLF, tab-separated file.
*/

#ifndef __CH4_COMMON_H
#define __CH4_COMMON_H

#include "city.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace ch4 {

// ---------------------------------------------------------------------------
// Dataset
// ---------------------------------------------------------------------------
/**
* Reads a Brazilian-cities file: <name> TAB <lat> TAB <long> CRLF, Latin-1.
*
* The caller owns the returned objects.
*/
inline std::vector<TCity *> loadCities(const std::string &fileName) {
    std::vector<TCity *> out;
    std::ifstream in(fileName.c_str());
    if (!in.is_open()) {
        std::cerr << "ERROR: cannot open " << fileName << "\n";
        std::exit(1);
    }
    char cityName[256];
    double dLat, dLong;
    while (in.getline(cityName, 200, '\t')) {
        in >> dLat;
        in >> dLong;
        in.ignore();
        out.push_back(new TCity(cityName, dLat, dLong));
    }
    in.close();
    return out;
}

inline void deleteAll(std::vector<TCity *> &v) {
    for (TCity *o : v) delete o;
    v.clear();
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
inline double mean(const std::vector<double> &v) {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

/** Linear-interpolation percentile on a COPY, so the input keeps its order. */
inline double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    if (v.size() == 1) return v[0];
    const double pos = p * static_cast<double>(v.size() - 1);
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi = std::min(lo + 1, v.size() - 1);
    const double frac = pos - static_cast<double>(lo);
    return v[lo] * (1.0 - frac) + v[hi] * frac;
}

inline double median(const std::vector<double> &v) { return percentile(v, 0.5); }

inline double stddev(const std::vector<double> &v) {
    if (v.size() < 2) return 0.0;
    const double m = mean(v);
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

// ---------------------------------------------------------------------------
// Brute force, for verifying exactness
// ---------------------------------------------------------------------------
/**
* Exact k-NN distances, ascending.
*
* @warning Pass an evaluator that is NOT the tree evaluator, otherwise these
* distances land in the counter the thesis reports per query.
*/
inline std::vector<double> bruteKnn(TCityDistanceEvaluator &eval,
                                    const std::vector<TCity *> &objects,
                                    TCity *query, unsigned int k) {
    std::vector<double> d;
    d.reserve(objects.size());
    for (TCity *o : objects) d.push_back(eval.GetDistance(*o, *query));
    const size_t kk = std::min<size_t>(k, d.size());
    std::partial_sort(d.begin(), d.begin() + kk, d.end());
    d.resize(kk);
    return d;
}

/** Exact range-query distances, ascending. Same evaluator warning as above. */
inline std::vector<double> bruteRange(TCityDistanceEvaluator &eval,
                                      const std::vector<TCity *> &objects,
                                      TCity *query, double radius) {
    std::vector<double> d;
    for (TCity *o : objects) {
        const double dist = eval.GetDistance(*o, *query);
        if (dist <= radius) d.push_back(dist);
    }
    std::sort(d.begin(), d.end());
    return d;
}

/** Compares two distance multisets with a tolerance. */
inline bool sameDistances(const std::vector<double> &a,
                          const std::vector<double> &b, double eps = 1e-9) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::fabs(a[i] - b[i]) > eps) return false;
    return true;
}

/** Sorted distances of every pair in a query result. */
template <class ResultType>
inline std::vector<double> resultDistances(ResultType *r) {
    std::vector<double> d;
    if (r == NULL) return d;
    for (unsigned int i = 0; i < r->GetNumOfEntries(); i++)
        d.push_back(r->GetPair(i)->GetDistance());
    std::sort(d.begin(), d.end());
    return d;
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
class Timer {
  public:
    Timer() : start_(std::chrono::steady_clock::now()) {}
    void reset() { start_ = std::chrono::steady_clock::now(); }
    double ms() const {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start_)
            .count();
    }

  private:
    std::chrono::steady_clock::time_point start_;
};

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------
/** Opens path in append mode, writing header first when the file is new. */
inline void openCsv(std::ofstream &os, const std::string &path,
                    const std::string &header) {
    if (path.empty()) return;
    std::error_code ec;
    const bool exists =
        std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 0;
    os.open(path, std::ios::app);
    if (!os) {
        std::cerr << "ERROR: cannot write " << path << "\n";
        std::exit(1);
    }
    if (!exists) os << header << "\n";
}

// ---------------------------------------------------------------------------
// Test reporting
// ---------------------------------------------------------------------------
/** Counts failed checks so main() can return a meaningful exit code. */
struct Checker {
    int failures = 0;
    int total = 0;

    void operator()(bool ok, const std::string &what) {
        total++;
        std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << "\n";
        if (!ok) failures++;
    }

    int report() {
        std::cout << "\n"
                  << (failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
                  << " (" << (total - failures) << "/" << total << " passed)\n";
        return failures == 0 ? 0 : 1;
    }
};

}  // namespace ch4

#endif  //__CH4_COMMON_H
