/**
* @file
*
* Etapa 2, top-down variant: a Slim-tree bulk-loaded from the hierarchy of the
* query centres (thesis section 4.4).
*
* WHY A SECOND VARIANT
* --------------------
* stSlimVoronoiRebuild only changes the ORDER in which objects reach the stock
* Add(); the split algorithm still decides every node, and the pivots appear
* nowhere in the result. Here the tree is built top-down and the queries decide
* the partition at every level:
*
*   Build(S, Q)          S = objects of this subtree, Q = query centres in it
*     if S fits in one page            -> a leaf
*     m  = fan-out: as many children as S needs, capped by what fits in a page
*     P  = k_q pivots from Q, by k-centre (farthest-point) over the queries
*        + m - k_q pivots from S, by k-centre continued from those
*     every object and every query goes to its nearest pivot (Voronoi)
*     recurse into each cell with its own objects and its own queries
*
* with k_q = round(QueryShare * m) when the cell holds queries, and 0 when it
* holds none. Two refinements keep the pages full:
*
*   - m is spread evenly over the index levels a cell needs, m = L^(1/h) for
*     L leaves and h levels, rather than full at the top and 2 at the bottom;
*   - a cell's subtrees are handed to the parent unjoined, and the parent
*     absorbs them whole while its page has room, so a cell slightly larger
*     than one page becomes two leaves of the parent, not an index node with
*     two entries and a level of its own.
*
* The query hierarchy is therefore built together with the tree: at the root
* the queries are grouped coarsely, each group follows its cell down, and is
* grouped again, more finely, one level below. Two consequences:
*
*   - A region that received queries gets a share of the fan-out out of
*     proportion to its share of the data, so its cells are SMALLER, its
*     subtrees SHALLOWER and its radii TIGHTER - the idea of an optimal search
*     tree, where the keys searched most often sit closest to the root.
*   - A region that received no queries is split by data pivots alone, into
*     cells as large as the page budget allows.
*
* The tree is NOT height-balanced: a leaf sits at the depth its cell needs.
* The Slim-tree query algorithms do not mind - they recurse by node type and
* use Height only for statistics - but the fat factor assumes every leaf at
* depth H, so it is not comparable for trees built here.
*
* With QueryShare = 0 the queries are ignored and the same code is a plain
* data-driven top-down bulk load. That is the CONTROL: the difference between
* the two isolates what the queries contribute, as etapa2_data does for the
* insertion-order variant.
*
* REPRESENTATIVES
* ---------------
* Pivots only partition; they never enter the tree, because query centres are
* not data. Representatives are chosen bottom-up, from the data:
*
*   leaf   the member that minimises the covering radius (the 1-centre)
*   index  the child representative r minimising max_c d(r, rep_c) + R_c,
*          which is exactly the radius the parent entry will store
*
* The second rule is forced, not chosen: a Slim-tree index node must contain
* its own representative as an entry with Distance 0, so rep(node) has to be
* the representative of one of its children.
*
* ACCOUNTING
* ----------
* Own evaluator: every distance spent choosing pivots, assigning cells and
* choosing representatives is reported by GetStats().Distances, and is the
* reorganisation cost to amortise.
*/

#ifndef __STSLIMQUERYBULKLOAD_H
#define __STSLIMQUERYBULKLOAD_H

#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimNode.h>
#include <arboretum/stPageManager.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <vector>

//=============================================================================
// Class template stSlimQueryBulkLoad
//-----------------------------------------------------------------------------
/**
* Top-down, query-steered bulk load of a Slim-tree.
*
* Typical use:
* @code
*   stSlimQueryBulkLoad<TCity, TCityDistanceEvaluator> bulk;
*   bulk.Build(newTree, newTreePageManager, objects, queryCentres);
* @endcode
*/
template <class ObjectType, class EvaluatorType>
class stSlimQueryBulkLoad{
   public:

      typedef stSlimTree<ObjectType, EvaluatorType> tSlimTree;

      struct Config{
         /**
         * Fraction of each node's fan-out given to query pivots when the cell
         * holds queries. 0 ignores the queries: the data-only control.
         */
         double QueryShare;

         /**
         * A cell holding fewer objects than this fraction of a full leaf is
         * dissolved into its neighbours, so a pivot that attracts almost
         * nothing does not cost a page of its own.
         */
         double MinCellFill;

         Config(): QueryShare(0.5), MinCellFill(0.25) {}
      };//end Config

      struct Stats{
         /** Distances spent building: pivots, cells and representatives. */
         long Distances;
         long LeafNodes;
         long IndexNodes;
         /** Depth of the deepest leaf, root = 1. */
         u_int32_t Height;
         /** Pivots chosen over all nodes, by origin. */
         long QueryPivots;
         long DataPivots;
         /** Cells dissolved for holding too few objects. */
         long CellsDissolved;

         Stats(): Distances(0), LeafNodes(0), IndexNodes(0), Height(0),
               QueryPivots(0), DataPivots(0), CellsDissolved(0) {}
      };//end Stats

      stSlimQueryBulkLoad(const Config & cfg = Config()): Cfg(cfg),
            PM(NULL), Objects(NULL), Queries(NULL), PageSize(0), CapLeaf(0),
            CapIndex(0) {}

      /**
      * Builds `target` from `objects`, steering the partition with `queries`.
      *
      * @param target  An EMPTY tree backed by `pm`.
      * @param pm      The page manager of `target`.
      * @param objects The data; serialised into the pages, never owned.
      * @param queries The observed query centres; they never enter the tree.
      * @return Objects stored.
      */
      long Build(tSlimTree * target, stPageManager * pm,
                 const std::vector<ObjectType *> & objects,
                 const std::vector<ObjectType *> & queries){
         St = Stats();
         LeafDepth.assign(objects.size(), 0);
         if ((target == NULL) || (pm == NULL) || objects.empty()){
            return 0;
         }//end if

         PM = pm;
         Objects = &objects;
         Queries = &queries;
         PageSize = pm->GetMinimumPageSize();

         // Page budgets. The fan-out cap uses the LARGEST object, so any m
         // children fit in an index page whatever their representatives are;
         // the leaf estimate uses the mean, and only sizes the fan-out.
         SerialSize.resize(objects.size());
         double sum = 0.0;
         u_int32_t largest = 0;
         for (std::size_t i = 0; i < objects.size(); i++){
            SerialSize[i] = objects[i]->GetSerializedSize();
            sum += SerialSize[i];
            largest = std::max(largest, SerialSize[i]);
         }//end for
         const double mean = sum / (double) objects.size();
         const u_int32_t room = PageSize - stSlimNode::GetGlobalOverhead();
         CapLeaf = std::max<std::size_t>(1, (std::size_t) (room /
               (mean + stSlimLeafNode::GetLeafEntryOverhead())));
         CapIndex = room / (largest + stSlimIndexNode::GetIndexEntryOverhead());
         if (CapIndex < 2){
            throw std::logic_error("stSlimQueryBulkLoad: page too small for "
                                   "two index entries.");
         }//end if

         std::vector<std::size_t> S(objects.size());
         for (std::size_t i = 0; i < S.size(); i++){
            S[i] = i;
         }//end for
         std::vector<std::size_t> Q(queries.size());
         for (std::size_t i = 0; i < Q.size(); i++){
            Q[i] = i;
         }//end for

         LeafMembers.clear();
         Sub root;
         if (FitsLeaf(S)){
            root = MakeLeaf(S, true);
         }else{
            std::vector<Sub> kids = Children(S, Q);
            root = MakeIndex(kids, true);
         }//end if
         St.Height = root.Height;
         target->AdoptBulkTree(root.PageID, root.Height,
                               (u_int32_t) objects.size(),
                               (u_int32_t) (St.LeafNodes + St.IndexNodes));

         // Subtrees may have been absorbed into their parents after their
         // leaves were written, so leaf depths are only known now.
         AssignDepth(root.PageID, 1);
         LeafMembers.clear();
         return (long) objects.size();
      }//end Build

      const Stats & GetStats() const { return St; }

      /** Depth (root = 1) of the leaf holding objects[i], as passed to Build. */
      const std::vector<int> & GetLeafDepth() const { return LeafDepth; }

   private:

      /** What a finished subtree tells its parent. */
      struct Sub{
         u_int32_t PageID;
         /** Index into Objects of the subtree representative. */
         std::size_t Rep;
         double Radius;
         u_int32_t NObjects;
         u_int32_t Height;
      };//end Sub

      Config Cfg;
      Stats St;
      EvaluatorType Evaluator;
      stPageManager * PM;
      const std::vector<ObjectType *> * Objects;
      const std::vector<ObjectType *> * Queries;
      std::vector<u_int32_t> SerialSize;
      std::vector<int> LeafDepth;
      /** Objects of each leaf page, until AssignDepth() has used them. */
      std::map<u_int32_t, std::vector<std::size_t> > LeafMembers;
      u_int32_t PageSize;
      std::size_t CapLeaf;
      std::size_t CapIndex;

      double Dist(ObjectType * a, ObjectType * b){
         St.Distances++;
         return Evaluator.GetDistance(*a, *b);
      }//end Dist

      ObjectType * Obj(std::size_t i){ return (*Objects)[i]; }
      ObjectType * Qry(std::size_t i){ return (*Queries)[i]; }

      /**
      * Fan-out for a subtree of n objects. It needs L = ceil(n / CapLeaf)
      * leaves, hence h = ceil(log_CapIndex L) index levels; spreading the
      * fan-out evenly, m = ceil(L^(1/h)), avoids the alternative of a full
      * fan-out at the top and index nodes with two children at the bottom,
      * which cost a level and buy nothing.
      */
      std::size_t FanOut(std::size_t n){
         const std::size_t leaves = (n + CapLeaf - 1) / CapLeaf;
         std::size_t m = leaves;
         if (leaves > CapIndex){
            const double h = std::ceil(std::log((double) leaves) /
                                       std::log((double) CapIndex));
            m = (std::size_t) std::ceil(std::pow((double) leaves, 1.0 / h) - 1e-9);
         }//end if
         return std::max<std::size_t>(2, std::min(m, CapIndex));
      }//end FanOut

      bool FitsLeaf(const std::vector<std::size_t> & S){
         u_int32_t used = stSlimNode::GetGlobalOverhead();
         for (std::size_t i = 0; i < S.size(); i++){
            used += SerialSize[S[i]] + stSlimLeafNode::GetLeafEntryOverhead();
            if (used > PageSize){
               return false;
            }//end if
         }//end for
         return true;
      }//end FitsLeaf

      //=====================================================================
      // The recursion
      //=====================================================================
      /**
      * The subtrees that together hold S, NOT yet joined under a node of
      * their own: a single leaf when S fits in one page, otherwise the
      * children an index node over S would have.
      *
      * Returning the children rather than the node lets the parent absorb a
      * small subtree whole. A cell slightly larger than one page splits into
      * two leaves; writing those as an index node with two entries would add
      * a level that costs a page read and saves nothing.
      */
      std::vector<Sub> Children(std::vector<std::size_t> & S,
                                std::vector<std::size_t> & Q){
         if (FitsLeaf(S)){
            return std::vector<Sub>(1, MakeLeaf(S, false));
         }//end if

         const std::size_t m = FanOut(S.size());

         std::size_t kq = 0;
         if (!Q.empty() && (Cfg.QueryShare > 0.0)){
            kq = (std::size_t) std::floor(Cfg.QueryShare * (double) m + 0.5);
            kq = std::max<std::size_t>(1, std::min(kq, std::min(m, Q.size())));
         }//end if

         std::vector<std::vector<std::size_t> > cellS, cellQ;
         Partition(S, Q, m, kq, cellS, cellQ);
         if (cellS.size() < 2){
            // Every object went to one query pivot. Partition on the data
            // alone, which always separates distinct objects.
            Partition(S, Q, m, 0, cellS, cellQ);
         }//end if
         if (cellS.size() < 2){
            // Only identical objects are left: any split is a valid one.
            Chunk(S, m, cellS, cellQ);
         }//end if

         std::vector<std::vector<Sub> > lists;
         for (std::size_t c = 0; c < cellS.size(); c++){
            if (!cellS[c].empty()){
               lists.push_back(Children(cellS[c], cellQ[c]));
            }//end if
         }//end for
         return Assemble(lists);
      }//end Children

      /**
      * Joins the subtree lists of the cells into the children of one node.
      * Smallest lists first: a list is absorbed whole while the page still
      * has room for it AND for one entry per list still to come; otherwise
      * it is written as an index node of its own.
      */
      std::vector<Sub> Assemble(std::vector<std::vector<Sub> > & lists){
         std::vector<std::size_t> order(lists.size());
         for (std::size_t i = 0; i < order.size(); i++){
            order[i] = i;
         }//end for
         std::stable_sort(order.begin(), order.end(), SizeLess(lists));

         std::vector<Sub> out;
         std::size_t pending = lists.size();
         for (std::size_t k = 0; k < order.size(); k++){
            std::vector<Sub> & list = lists[order[k]];
            pending--;
            if ((list.size() == 1) ||
                (out.size() + list.size() + pending <= CapIndex)){
               out.insert(out.end(), list.begin(), list.end());
            }else{
               out.push_back(MakeIndex(list, false));
            }//end if
         }//end for
         return out;
      }//end Assemble

      struct SizeLess{
         const std::vector<std::vector<Sub> > & L;
         SizeLess(const std::vector<std::vector<Sub> > & l): L(l) {}
         bool operator()(std::size_t a, std::size_t b) const {
            return L[a].size() < L[b].size();
         }//end operator()
      };//end SizeLess

      /** Records the depth of every object, walking down from the root. */
      void AssignDepth(u_int32_t pageID, int depth){
         stPage * page = PM->GetPage(pageID);
         stSlimNode * node = stSlimNode::CreateNode(page);
         std::vector<u_int32_t> kids;
         if (node->GetNodeType() == stSlimNode::INDEX){
            stSlimIndexNode * index = (stSlimIndexNode *) node;
            for (u_int32_t i = 0; i < index->GetNumberOfEntries(); i++){
               kids.push_back(index->GetIndexEntry(i).PageID);
            }//end for
         }else{
            const std::vector<std::size_t> & members = LeafMembers[pageID];
            for (std::size_t i = 0; i < members.size(); i++){
               LeafDepth[members[i]] = depth;
            }//end for
         }//end if
         delete node;
         PM->ReleasePage(page);
         for (std::size_t i = 0; i < kids.size(); i++){
            AssignDepth(kids[i], depth + 1);
         }//end for
      }//end AssignDepth

      /**
      * Chooses m pivots (kq of them from the queries), assigns objects and
      * queries to the nearest one, and dissolves cells that are too small.
      * Only non-empty cells are returned.
      */
      void Partition(const std::vector<std::size_t> & S,
                     const std::vector<std::size_t> & Q, std::size_t m,
                     std::size_t kq,
                     std::vector<std::vector<std::size_t> > & cellS,
                     std::vector<std::vector<std::size_t> > & cellQ){
         cellS.clear();
         cellQ.clear();
         std::vector<ObjectType *> pivots;

         // k-centre over the queries.
         if (kq > 0){
            std::vector<double> best(Q.size(), 1e300);
            std::size_t next = 0;
            while (pivots.size() < kq){
               pivots.push_back(Qry(Q[next]));
               St.QueryPivots++;
               double far = -1.0;
               for (std::size_t i = 0; i < Q.size(); i++){
                  best[i] = std::min(best[i], Dist(Qry(Q[i]), pivots.back()));
                  if (best[i] > far){
                     far = best[i];
                     next = i;
                  }//end if
               }//end for
               if (far <= 0.0){
                  break;   // every query already coincides with a pivot
               }//end if
            }//end while
         }//end if

         // k-centre over the objects, continued from the query pivots. The
         // distances it computes ARE the object-pivot matrix, so the Voronoi
         // assignment below costs nothing extra.
         const std::size_t n = S.size();
         std::vector<std::vector<double> > dS(n);
         std::vector<double> best(n, 1e300);
         for (std::size_t p = 0; p < pivots.size(); p++){
            for (std::size_t i = 0; i < n; i++){
               dS[i].push_back(Dist(Obj(S[i]), pivots[p]));
               best[i] = std::min(best[i], dS[i].back());
            }//end for
         }//end for
         std::size_t next = 0;
         if (!pivots.empty()){
            next = (std::size_t) (std::max_element(best.begin(), best.end()) -
                                  best.begin());
         }//end if
         while ((pivots.size() < m) && (pivots.empty() || (best[next] > 0.0))){
            pivots.push_back(Obj(S[next]));
            St.DataPivots++;
            double far = -1.0;
            for (std::size_t i = 0; i < n; i++){
               dS[i].push_back(Dist(Obj(S[i]), pivots.back()));
               best[i] = std::min(best[i], dS[i].back());
               if (best[i] > far){
                  far = best[i];
                  next = i;
               }//end if
            }//end for
         }//end while

         const std::size_t np = pivots.size();
         std::vector<std::vector<double> > dQ(Q.size());
         for (std::size_t i = 0; i < Q.size(); i++){
            for (std::size_t p = 0; p < np; p++){
               dQ[i].push_back(Dist(Qry(Q[i]), pivots[p]));
            }//end for
         }//end for

         // Voronoi assignment, then dissolve undersized cells: the smallest
         // goes first, and its members move to their nearest surviving pivot.
         std::vector<bool> alive(np, true);
         std::vector<std::size_t> ownS(n), ownQ(Q.size());
         std::vector<std::size_t> count(np, 0);
         for (std::size_t i = 0; i < n; i++){
            ownS[i] = Nearest(dS[i], alive);
            count[ownS[i]]++;
         }//end for
         const std::size_t minCell = (std::size_t) std::floor(
               Cfg.MinCellFill * (double) CapLeaf);
         std::size_t living = np;
         for (std::size_t p = 0; p < np; p++){
            if (count[p] == 0){
               alive[p] = false;
               living--;
            }//end if
         }//end for
         while (living > 2){
            std::size_t worst = np;
            for (std::size_t p = 0; p < np; p++){
               if (alive[p] && ((worst == np) || (count[p] < count[worst]))){
                  worst = p;
               }//end if
            }//end for
            if (count[worst] >= minCell){
               break;
            }//end if
            alive[worst] = false;
            living--;
            St.CellsDissolved++;
            for (std::size_t i = 0; i < n; i++){
               if (ownS[i] == worst){
                  ownS[i] = Nearest(dS[i], alive);
                  count[ownS[i]]++;
               }//end if
            }//end for
            count[worst] = 0;
         }//end while
         for (std::size_t i = 0; i < Q.size(); i++){
            ownQ[i] = Nearest(dQ[i], alive);
         }//end for

         std::vector<int> slot(np, -1);
         for (std::size_t p = 0; p < np; p++){
            if (alive[p]){
               slot[p] = (int) cellS.size();
               cellS.push_back(std::vector<std::size_t>());
               cellQ.push_back(std::vector<std::size_t>());
            }//end if
         }//end for
         for (std::size_t i = 0; i < n; i++){
            cellS[slot[ownS[i]]].push_back(S[i]);
         }//end for
         for (std::size_t i = 0; i < Q.size(); i++){
            cellQ[slot[ownQ[i]]].push_back(Q[i]);
         }//end for
      }//end Partition

      /** Nearest living pivot; ties go to the lowest index. */
      static std::size_t Nearest(const std::vector<double> & d,
                                 const std::vector<bool> & alive){
         std::size_t best = d.size();
         for (std::size_t p = 0; p < d.size(); p++){
            if (alive[p] && ((best == d.size()) || (d[p] < d[best]))){
               best = p;
            }//end if
         }//end for
         return best;
      }//end Nearest

      /** Last resort for identical objects: m consecutive chunks. */
      void Chunk(const std::vector<std::size_t> & S, std::size_t m,
                 std::vector<std::vector<std::size_t> > & cellS,
                 std::vector<std::vector<std::size_t> > & cellQ){
         cellS.assign(m, std::vector<std::size_t>());
         cellQ.assign(m, std::vector<std::size_t>());
         for (std::size_t i = 0; i < S.size(); i++){
            cellS[i * m / S.size()].push_back(S[i]);
         }//end for
      }//end Chunk

      //=====================================================================
      // Writing nodes
      //=====================================================================
      Sub MakeLeaf(const std::vector<std::size_t> & S, bool isRoot){
         const std::size_t n = S.size();
         std::vector<std::vector<double> > d(n, std::vector<double>(n, 0.0));
         for (std::size_t i = 0; i < n; i++){
            for (std::size_t j = i + 1; j < n; j++){
               d[i][j] = d[j][i] = Dist(Obj(S[i]), Obj(S[j]));
            }//end for
         }//end for
         std::size_t rep = 0;
         double radius = 1e300;
         for (std::size_t i = 0; i < n; i++){
            const double r = *std::max_element(d[i].begin(), d[i].end());
            if (r < radius){
               radius = r;
               rep = i;
            }//end if
         }//end for

         stPage * page = PM->GetNewPage();
         stSlimLeafNode * leaf = new stSlimLeafNode(page, true);
         for (std::size_t i = 0; i < n; i++){
            const int idx = leaf->AddEntry(SerialSize[S[i]], Obj(S[i])->Serialize());
            if (idx < 0){
               throw std::logic_error("stSlimQueryBulkLoad: leaf overflow.");
            }//end if
            // A root has no representative above it, so its entries carry
            // Distance 0, as AddNewRoot() writes them.
            leaf->GetLeafEntry(idx).Distance = isRoot ? 0.0 : d[i][rep];
         }//end for
         PM->WritePage(page);
         LeafMembers[page->GetPageID()] = S;
         Sub out;
         out.PageID = page->GetPageID();
         out.Rep = S[rep];
         out.Radius = radius;
         out.NObjects = (u_int32_t) n;
         out.Height = 1;
         delete leaf;
         PM->ReleasePage(page);
         St.LeafNodes++;
         return out;
      }//end MakeLeaf

      Sub MakeIndex(const std::vector<Sub> & children, bool isRoot){
         const std::size_t k = children.size();
         std::vector<std::vector<double> > d(k, std::vector<double>(k, 0.0));
         for (std::size_t a = 0; a < k; a++){
            for (std::size_t b = a + 1; b < k; b++){
               d[a][b] = d[b][a] = Dist(Obj(children[a].Rep), Obj(children[b].Rep));
            }//end for
         }//end for
         std::size_t rep = 0;
         double radius = 1e300;
         for (std::size_t a = 0; a < k; a++){
            double r = 0.0;
            for (std::size_t b = 0; b < k; b++){
               r = std::max(r, d[a][b] + children[b].Radius);
            }//end for
            if (r < radius){
               radius = r;
               rep = a;
            }//end if
         }//end for

         stPage * page = PM->GetNewPage();
         stSlimIndexNode * node = new stSlimIndexNode(page, true);
         u_int32_t total = 0;
         u_int32_t height = 0;
         for (std::size_t c = 0; c < k; c++){
            const std::size_t o = children[c].Rep;
            const int idx = node->AddEntry(SerialSize[o], Obj(o)->Serialize());
            if (idx < 0){
               throw std::logic_error("stSlimQueryBulkLoad: index overflow.");
            }//end if
            node->GetIndexEntry(idx).PageID = children[c].PageID;
            node->GetIndexEntry(idx).Distance = isRoot ? 0.0 : d[c][rep];
            node->GetIndexEntry(idx).Radius = children[c].Radius;
            node->GetIndexEntry(idx).NEntries = children[c].NObjects;
            total += children[c].NObjects;
            height = std::max(height, children[c].Height);
         }//end for
         PM->WritePage(page);
         Sub out;
         out.PageID = page->GetPageID();
         out.Rep = children[rep].Rep;
         out.Radius = radius;
         out.NObjects = total;
         out.Height = height + 1;
         delete node;
         PM->ReleasePage(page);
         St.IndexNodes++;
         return out;
      }//end MakeIndex

};//end stSlimQueryBulkLoad

#endif //__STSLIMQUERYBULKLOAD_H
