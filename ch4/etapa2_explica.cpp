// etapa2_explica.cpp - mostra, passo a passo e com os dados reais, como a
// Etapa 2 constroi T'. Nao mede nada: so imprime o que acontece.
//
// Reproduz exatamente o pipeline de chapter4.cpp (mesmo gerador de carga,
// mesma semente, pmNODE_REPS), e acrescenta a inspecao de T_Q e de T'.
//
//   bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh etapa2_explica -- --regime concentrado

#include <arboretum/stPlainDiskPageManager.h>
#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimNode.h>
#include <arboretum/stSlimVoronoiRebuild.h>
#include "ch4_common.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

typedef stSlimTree<TCity, TCityDistanceEvaluator> mySlimTree;
typedef stSlimVoronoiRebuild<TCity, TCityDistanceEvaluator> myRebuild;

// --- copia de chapter4.cpp (static la) -------------------------------------
static std::vector<size_t> farthestPointSample(TCityDistanceEvaluator &eval,
                                               const std::vector<TCity *> &objects,
                                               int howMany, unsigned int seed) {
    std::vector<size_t> chosen;
    std::mt19937 rng(seed);
    chosen.push_back(rng() % objects.size());
    std::vector<double> best(objects.size(), 1e300);
    while ((int)chosen.size() < howMany) {
        TCity *last = objects[chosen.back()];
        size_t far = 0;
        double farDist = -1.0;
        for (size_t i = 0; i < objects.size(); i++) {
            const double d = eval.GetDistance(*objects[i], *last);
            if (d < best[i]) best[i] = d;
            if (best[i] > farDist) { farDist = best[i]; far = i; }
        }
        chosen.push_back(far);
    }
    return chosen;
}

static std::string key(TCity *c) {
    std::ostringstream s;
    s << c->GetName() << '|' << c->GetLatitude() << '|' << c->GetLongitude();
    return s.str();
}

static std::string show(TCity *c) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%-32s (%7.2f, %6.2f)", c->GetName().c_str(),
                  c->GetLatitude(), c->GetLongitude());
    return buf;
}

// Conta, para cada celula, quantos objetos ha sob a pagina `pid` de uma arvore.
static void countUnder(stPageManager *pm, u_int32_t pid,
                       const std::map<std::string, int> &cellOf,
                       std::map<int, int> &acc) {
    stPage *page = pm->GetPage(pid);
    stSlimNode *node = stSlimNode::CreateNode(page);
    if (node->GetNodeType() == stSlimNode::INDEX) {
        stSlimIndexNode *in = (stSlimIndexNode *)node;
        std::vector<u_int32_t> kids;
        for (u_int32_t i = 0; i < in->GetNumberOfEntries(); i++)
            kids.push_back(in->GetIndexEntry(i).PageID);
        delete node;
        pm->ReleasePage(page);
        for (u_int32_t k : kids) countUnder(pm, k, cellOf, acc);
        return;
    }
    stSlimLeafNode *lf = (stSlimLeafNode *)node;
    for (u_int32_t i = 0; i < lf->GetNumberOfEntries(); i++) {
        TCity c;
        c.Unserialize(lf->GetObject(i), lf->GetObjectSize(i));
        auto it = cellOf.find(key(&c));
        acc[it == cellOf.end() ? -1 : it->second]++;
    }
    delete node;
    pm->ReleasePage(page);
}

// Imprime a raiz de uma arvore: representante, raio e composicao por celula.
static void dumpRoot(const char *title, stPageManager *pm, u_int32_t root,
                     const std::map<std::string, int> &cellOf) {
    stPage *page = pm->GetPage(root);
    stSlimNode *node = stSlimNode::CreateNode(page);
    std::cout << "\n" << title << "\n";
    if (node->GetNodeType() != stSlimNode::INDEX) {
        std::cout << "  (a raiz e uma folha)\n";
        delete node; pm->ReleasePage(page);
        return;
    }
    stSlimIndexNode *in = (stSlimIndexNode *)node;
    struct E { TCity rep; u_int32_t pid; double r; u_int32_t n; };
    std::vector<E> es;
    for (u_int32_t i = 0; i < in->GetNumberOfEntries(); i++) {
        E e;
        e.rep.Unserialize(in->GetObject(i), in->GetObjectSize(i));
        e.pid = in->GetIndexEntry(i).PageID;
        e.r = in->GetIndexEntry(i).Radius;
        e.n = in->GetIndexEntry(i).NEntries;
        es.push_back(e);
    }
    delete node;
    pm->ReleasePage(page);
    std::cout << "  " << es.size() << " entradas na raiz\n";
    for (size_t i = 0; i < es.size(); i++) {
        std::map<int, int> acc;
        countUnder(pm, es[i].pid, cellOf, acc);
        int tot = 0;
        for (auto &kv : acc) tot += kv.second;
        char buf[64];
        std::snprintf(buf, sizeof buf, "  [%2zu] r=%5.2f  objs=%4d  ", i, es[i].r, tot);
        std::cout << buf << show(&es[i].rep) << "\n        celulas:";
        for (auto &kv : acc) std::cout << " C" << kv.first << "=" << kv.second;
        std::cout << "\n";
    }
}

int main(int argc, char **argv) {
    std::string dataset, regime = "concentrado", indexDir = ".";
    int adaptQ = 200, measureQ = 100, centresOpt = 0, k = 10;
    unsigned seed = 42, pageSize = 1024;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (a == "--dataset") dataset = next();
        else if (a == "--regime") regime = next();
        else if (a == "--index-dir") indexDir = next();
        else if (a == "--seed") seed = std::stoul(next());
        else if (a == "--centres") centresOpt = std::stoi(next());
        else if (a == "--page-size") pageSize = std::stoul(next());
        else if (a == "--queryfile") next();
    }
    if (dataset.empty()) { std::cout << "uso: --dataset F [--regime R]" << std::endl; return 0; }
    std::vector<TCity *> objects = ch4::loadCities(dataset);
    TCityDistanceEvaluator eval;

    // ---- PASSO 0: a carga de consultas (identica a chapter4.cpp) ----------
    const int centres = centresOpt > 0 ? centresOpt : (regime == "concentrado" ? 2 : 8);
    const int wanted = 2 * std::max(adaptQ, measureQ);
    const std::vector<size_t> seeds = farthestPointSample(eval, objects, centres, seed);
    std::vector<TCity *> all;
    std::vector<int> hotOf;
    const int perCentre = (wanted + centres - 1) / centres;
    for (size_t s = 0; s < seeds.size(); s++) {
        std::vector<std::pair<double, size_t> > byDist;
        for (size_t i = 0; i < objects.size(); i++)
            byDist.push_back({eval.GetDistance(*objects[i], *objects[seeds[s]]), i});
        const size_t take = std::min<size_t>(perCentre, byDist.size());
        std::partial_sort(byDist.begin(), byDist.begin() + take, byDist.end());
        for (size_t i = 0; i < take; i++) { all.push_back(objects[byDist[i].second]); hotOf.push_back((int)s); }
    }
    std::vector<TCity *> adapt;
    std::vector<int> adaptHot;
    for (size_t i = 0; i < all.size() && (int)adapt.size() < adaptQ; i++)
        if (i % 2) { adapt.push_back(all[i]); adaptHot.push_back(hotOf[i]); }

    std::cout << "=== PASSO 0: carga '" << regime << "', " << objects.size()
              << " cidades indexadas, " << adapt.size() << " consultas de adaptacao ===\n";
    for (size_t s = 0; s < seeds.size(); s++) {
        int n = 0;
        for (int h : adaptHot) n += (h == (int)s);
        std::cout << "  hotspot H" << s << ": " << show(objects[seeds[s]])
                  << "  -> " << n << " consultas nas cidades vizinhas\n";
    }
    std::cout << "  primeiras 8 consultas (o CENTRO de cada uma e uma cidade):\n";
    for (int i = 0; i < 8 && i < (int)adapt.size(); i++)
        std::cout << "    q" << i << " = " << show(adapt[i]) << "  [H" << adaptHot[i] << "]\n";
    std::cout << "  Obs.: so o centro entra na T_Q. O resultado do k-NN (os " << k
              << " vizinhos) nao e usado na Etapa 2.\n";

    // ---- PASSO 1: T_Q -----------------------------------------------------
    const std::string tqPath = indexDir + "/explica_TQ.dat";
    std::remove(tqPath.c_str());
    stPlainDiskPageManager *tqpm = new stPlainDiskPageManager(tqPath.c_str(), pageSize);
    mySlimTree *tq = new mySlimTree(tqpm);
    for (TCity *q : adapt) tq->Add(q->Clone());
    std::cout << "\n=== PASSO 1: T_Q = Slim-tree com os " << tq->GetNumberOfObjects()
              << " centros de consulta; altura " << tq->GetHeight() << " ===\n";
    std::map<std::string, int> dummy;  // na T_Q nao ha celulas ainda
    dumpRoot("  Raiz da T_Q (cada entrada = um subgrupo de consultas):", tqpm,
             tq->GetRootPageID(), dummy);

    // ---- PASSO 2-3: pivos e celulas --------------------------------------
    myRebuild rb;
    rb.BuildQueryTree(adapt, pageSize, indexDir + "/explica_TQ2.dat");
    const size_t P = rb.ExtractPivots(myRebuild::pmNODE_REPS);
    rb.AssignCells(objects);
    std::vector<size_t> sizes = rb.GetCellSizes();
    std::cout << "\n=== PASSO 2: pivos = representantes da RAIZ da T_Q -> |P| = " << P
              << " (de " << adapt.size() << " consultas) ===\n";
    std::cout << "=== PASSO 3: cada uma das " << objects.size()
              << " cidades vai para o pivo mais proximo (Voronoi) ===\n";

    // Para saber o pivo de cada celula, refazemos o argmin (mesma regra).
    // Os pivos sao, por construcao, os mesmos representantes listados acima.
    std::vector<TCity *> pivots;
    {
        stPage *page = tqpm->GetPage(tq->GetRootPageID());
        stSlimNode *node = stSlimNode::CreateNode(page);
        stSlimIndexNode *in = (stSlimIndexNode *)node;
        for (u_int32_t i = 0; i < in->GetNumberOfEntries(); i++) {
            TCity *c = new TCity();
            c->Unserialize(in->GetObject(i), in->GetObjectSize(i));
            pivots.push_back(c);
        }
        delete node;
        tqpm->ReleasePage(page);
    }
    std::map<std::string, int> cellOf;
    std::vector<std::vector<std::pair<double, TCity *> > > cells(pivots.size());
    for (TCity *o : objects) {
        int b = 0; double bd = eval.GetDistance(*o, *pivots[0]);
        for (size_t p = 1; p < pivots.size(); p++) {
            double d = eval.GetDistance(*o, *pivots[p]);
            if (d < bd) { bd = d; b = (int)p; }
        }
        cellOf[key(o)] = b;
        cells[b].push_back({bd, o});
    }
    for (size_t p = 0; p < pivots.size(); p++) {
        std::sort(cells[p].begin(), cells[p].end(),
                  [](const std::pair<double, TCity *> &a, const std::pair<double, TCity *> &b) {
                      return a.first < b.first; });
        char buf[80];
        std::snprintf(buf, sizeof buf, "  C%-2zu pivo ", p);
        std::cout << buf << show(pivots[p]) << "  |celula| = " << cells[p].size()
                  << (cells[p].size() == sizes[p] ? "" : "  (DIVERGE da lib!)") << "\n";
    }

    // ---- PASSO 4: ordem de insercao em T' --------------------------------
    std::vector<size_t> order(pivots.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return cells[a].size() > cells[b].size(); });
    std::cout << "\n=== PASSO 4: T' vazia recebe as cidades nesta ordem ===\n"
              << "  celulas da maior para a menor:";
    for (size_t c : order) std::cout << " C" << c << "(" << cells[c].size() << ")";
    std::cout << "\n  dentro de cada celula, da mais proxima do pivo para a mais longe.\n"
              << "  Inicio da fila (celula C" << order[0] << "):\n";
    for (int i = 0; i < 6 && i < (int)cells[order[0]].size(); i++)
        std::printf("    #%-4d d=%5.2f  %s\n", i + 1, cells[order[0]][i].first,
                    show(cells[order[0]][i].second).c_str());
    std::cout << "    ...\n";
    {
        auto &c = cells[order[0]];
        std::printf("    #%-4zu d=%5.2f  %s   <- ultima de C%zu\n", c.size(), c.back().first,
                    show(c.back().second).c_str(), order[0]);
        auto &c2 = cells[order[1]];
        std::printf("    #%-4zu d=%5.2f  %s   <- primeira de C%zu\n", c.size() + 1,
                    c2.front().first, show(c2.front().second).c_str(), order[1]);
    }
    std::cout << "  Cada insercao usa o Add() normal da Slim-tree (ChooseSubtree + split MST).\n";

    const std::string tpPath = indexDir + "/explica_Tlinha.dat";
    std::remove(tpPath.c_str());
    stPlainDiskPageManager *tppm = new stPlainDiskPageManager(tpPath.c_str(), pageSize);
    mySlimTree *tp = new mySlimTree(tppm);
    rb.BuildTree(tp, objects, false);
    std::cout << "\n=== RESULTADO: T' tem " << tp->GetNumberOfObjects() << " cidades, altura "
              << tp->GetHeight() << ", " << tp->GetNodeCount() << " nos ===";
    dumpRoot("  Raiz de T' (quem sao os representantes e que celulas cada subarvore contem):",
             tppm, tp->GetRootPageID(), cellOf);

    // Comparacao: a arvore original (ordem do arquivo).
    const std::string orPath = indexDir + "/explica_orig.dat";
    std::remove(orPath.c_str());
    stPlainDiskPageManager *orpm = new stPlainDiskPageManager(orPath.c_str(), pageSize);
    mySlimTree *orig = new mySlimTree(orpm);
    for (TCity *o : objects) orig->Add(o->Clone());
    dumpRoot("  Para comparar - raiz da arvore ORIGINAL (ordem do arquivo):",
             orpm, orig->GetRootPageID(), cellOf);

    delete tp; delete tppm; delete orig; delete orpm; delete tq; delete tqpm;
    for (TCity *p : pivots) delete p;
    std::remove(tqPath.c_str()); std::remove(tpPath.c_str()); std::remove(orPath.c_str());
    ch4::deleteAll(objects);
    return 0;
}
