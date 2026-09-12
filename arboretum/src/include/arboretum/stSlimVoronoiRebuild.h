/**
* @file
*
* Etapa 2 of the adaptive Slim-tree: query tree and batch reorganisation
* (thesis section 4.4).
*
* WHAT IT DOES
* ------------
*   1. The centres of the queries observed so far are inserted, as objects, into
*      a SECOND metric tree T_Q over the same space.
*   2. Pivots P are extracted from T_Q - its node representatives.
*   3. Every indexed object x is assigned to its nearest pivot,
*
*        (4.9)  pi(x) = argmin over p in P of d(x, p)
*
*      which is a Voronoi partition of the data induced by the QUERIES.
*   4. A NEW Slim-tree T' is built by inserting the objects cell by cell, so
*      that objects in the same Voronoi cell tend to land in the same nodes.
*
* WHY IT IS A DIFFERENT REGIME FROM ETAPA 1
* -----------------------------------------
* Etapa 1 edits the tree continuously, one query at a time, and its cost is
* spread over the workload. Etapa 2 leaves the data tree completely untouched
* while it accumulates query centres, then pays for one reorganisation - the
* regime of the classic Slim-Down, but with a criterion that is no longer the
* covering radii: it is the Voronoi structure of the queries actually observed.
*
* The thesis says the two are mutually exclusive alternatives, and they are:
* Etapa 1 needs a single region of concentration to converge towards (H1), and
* oscillates when queries are spread over several separated regions. There the
* pivot set simply grows to cover every region that was queried. Etapa 2 works
* best exactly where Etapa 1 struggles.
*
* HOW IT DIFFERS FROM THE VD-tree
* -------------------------------
* Moriyama et al. (2021) also partition a metric space with Voronoi diagrams,
* but their pivots come from the DATA. Here they come from the distribution of
* the QUERIES. That is the whole point of section 4.4.
*
* TWO GUARANTEES THIS FILE KEEPS
* ------------------------------
*   - The ORIGINAL tree is never modified. Section 4.4.1 requires both
*     organisations to be measurable under the same workload, so both must
*     exist at once.
*   - Query centres NEVER become indexed data. They orient the organisation and
*     nothing more. BuildTree() only ever iterates the caller's object vector,
*     and the assertion T'->GetNumberOfObjects() == objects.size() makes that
*     checkable in one line.
*
* ACCOUNTING
* ----------
* Everything here uses its OWN EvaluatorType instance, so the cost of Etapa 2 is
* disjoint from the query cost by construction rather than by subtraction. T_Q
* likewise gets its OWN page manager, so its page reads and writes are counted
* separately from the data tree ones.
*
* @warning T_Q is backed by a disk page manager on a scratch file, NOT by
* stMemoryPageManager, which would otherwise be the natural choice. In this
* version of arboretum stMemoryPageManager is broken: a stSlimTree built on one
* segfaults on the SECOND insertion, reproducibly and independently of anything
* in Chapter 4. Nothing else in the library uses that page manager, so the fault
* had gone unnoticed. Since the accounting argument is about having a SEPARATE
* manager rather than about avoiding disk, a scratch file costs nothing that
* matters here. The file is removed when Clear() runs.
*/

#ifndef __STSLIMVORONOIREBUILD_H
#define __STSLIMVORONOIREBUILD_H

#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimNode.h>
#include <arboretum/stPlainDiskPageManager.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <ostream>
#include <string>
#include <vector>

//=============================================================================
// Class template stSlimVoronoiRebuild
//-----------------------------------------------------------------------------
/**
* Batch, query-driven reorganisation of a Slim-tree (thesis section 4.4).
*
* Typical use:
* @code
*   stSlimVoronoiRebuild<TCity, TCityDistanceEvaluator> rebuild;
*   rebuild.BuildQueryTree(queryCentres, 1024);        // T_Q
*   rebuild.ExtractPivots();                           // P
*   rebuild.AssignCells(objects);                      // Equation 4.9
*   rebuild.BuildTree(newTree, objects);               // T'
* @endcode
*/
template <class ObjectType, class EvaluatorType>
class stSlimVoronoiRebuild{
   public:

      typedef stSlimTree<ObjectType, EvaluatorType> tSlimTree;

      /**
      * Where the pivots come from. The choice fixes |P|, which is the single
      * most important knob of Etapa 2 and is worth sweeping.
      */
      enum tPivotMode{
         /** Representatives of T_Q index nodes: a coarse partition. */
         pmNODE_REPS,
         /** Representatives of T_Q leaves: finer. */
         pmLEAF_REPS,
         /** Every query centre: one cell per query. A control, not a proposal. */
         pmALL_CENTRES
      };

      stSlimVoronoiRebuild(){
         QueryTree = NULL;
         QueryPageManager = NULL;
         AssignDistances = 0;
         PivotDistances = 0;
      }//end stSlimVoronoiRebuild

      ~stSlimVoronoiRebuild(){
         Clear();
      }//end ~stSlimVoronoiRebuild

      /** Releases T_Q, the pivots and the cell assignment. */
      void Clear(){
         if (QueryTree != NULL){
            delete QueryTree;
            QueryTree = NULL;
         }//end if
         if (QueryPageManager != NULL){
            delete QueryPageManager;
            QueryPageManager = NULL;
            if (!QueryIndexPath.empty()){
               std::remove(QueryIndexPath.c_str());
            }//end if
         }//end if
         for (std::size_t i = 0; i < Pivots.size(); i++){
            delete Pivots[i];
         }//end for
         Pivots.clear();
         CellOf.clear();
         DistToPivot.clear();
      }//end Clear

      //=====================================================================
      // Step 1 - the query tree T_Q
      //=====================================================================
      /**
      * Indexes the observed query centres in a second metric tree.
      *
      * T_Q is small by construction: the number of queries in a time window is
      * typically far below the number of indexed objects, which is precisely
      * the argument section 4.4 makes for its low maintenance cost. The centres
      * are CLONED in, so the caller keeps ownership of its own copies.
      *
      * @param queryCentres The observed query centres.
      * @param pageSize     Page size for T_Q.
      * @param indexPath    Scratch file for T_Q. It is created fresh and deleted
      *                     by Clear(). See the warning at the top of this file
      *                     for why this is not an in-memory page manager.
      */
      void BuildQueryTree(const std::vector<ObjectType *> & queryCentres,
                          u_int32_t pageSize = 1024,
                          const std::string & indexPath = "TQ_index.dat"){
         Clear();
         if (queryCentres.empty()){
            return;
         }//end if

         QueryIndexPath = indexPath;
         std::remove(QueryIndexPath.c_str());
         QueryPageManager = new stPlainDiskPageManager(QueryIndexPath.c_str(),
                                                       pageSize);
         QueryTree = new tSlimTree(QueryPageManager);
         for (std::size_t i = 0; i < queryCentres.size(); i++){
            ObjectType * clone = queryCentres[i]->Clone();
            QueryTree->Add(clone);
         }//end for
      }//end BuildQueryTree

      /** Number of query centres currently indexed in T_Q. */
      long GetQueryTreeSize() const {
         return (QueryTree != NULL) ? QueryTree->GetNumberOfObjects() : 0;
      }//end GetQueryTreeSize

      u_int32_t GetQueryTreeHeight() const {
         return (QueryTree != NULL) ? QueryTree->GetHeight() : 0;
      }//end GetQueryTreeHeight

      //=====================================================================
      // Step 2 - the pivots (section 4.4.1)
      //=====================================================================
      /**
      * Extracts the pivot set from T_Q.
      *
      * @param mode Which representatives to take.
      * @return |P|.
      */
      std::size_t ExtractPivots(tPivotMode mode = pmNODE_REPS){
         for (std::size_t i = 0; i < Pivots.size(); i++){
            delete Pivots[i];
         }//end for
         Pivots.clear();

         if ((QueryTree == NULL) || (QueryTree->GetRootPageID() == 0)){
            return 0;
         }//end if
         CollectPivots(QueryTree->GetRootPageID(), mode);
         return Pivots.size();
      }//end ExtractPivots

      /**
      * Uses an explicit pivot set instead of deriving one from T_Q.
      *
      * Useful for controls and for sweeping |P| independently of T_Q shape.
      * The objects are cloned.
      */
      void SetPivots(const std::vector<ObjectType *> & pivots){
         for (std::size_t i = 0; i < Pivots.size(); i++){
            delete Pivots[i];
         }//end for
         Pivots.clear();
         for (std::size_t i = 0; i < pivots.size(); i++){
            Pivots.push_back(pivots[i]->Clone());
         }//end for
      }//end SetPivots

      std::size_t GetPivotCount() const { return Pivots.size(); }

      //=====================================================================
      // Step 3 - the Voronoi assignment (Equation 4.9)
      //=====================================================================
      /**
      * Assigns every object to its nearest pivot.
      *
      * Costs exactly |objects| * |P| distances, all charged to this class own
      * evaluator and reported by GetAssignDistances(). Ties go to the lowest
      * pivot index, which keeps the partition deterministic.
      */
      void AssignCells(const std::vector<ObjectType *> & objects){
         CellOf.assign(objects.size(), -1);
         DistToPivot.assign(objects.size(), 0.0);
         if (Pivots.empty()){
            return;
         }//end if

         for (std::size_t i = 0; i < objects.size(); i++){
            int best = 0;
            double bestDist = Evaluator.GetDistance(*objects[i], *Pivots[0]);
            AssignDistances++;
            for (std::size_t p = 1; p < Pivots.size(); p++){
               const double d = Evaluator.GetDistance(*objects[i], *Pivots[p]);
               AssignDistances++;
               if (d < bestDist){
                  bestDist = d;
                  best = (int) p;
               }//end if
            }//end for
            CellOf[i] = best;
            DistToPivot[i] = bestDist;
         }//end for
      }//end AssignCells

      /** Objects per cell, indexed by pivot. */
      std::vector<std::size_t> GetCellSizes() const {
         std::vector<std::size_t> sizes(Pivots.size(), 0);
         for (std::size_t i = 0; i < CellOf.size(); i++){
            if (CellOf[i] >= 0){
               sizes[CellOf[i]]++;
            }//end if
         }//end for
         return sizes;
      }//end GetCellSizes

      long GetAssignDistances() const { return AssignDistances; }
      long GetPivotDistances() const { return PivotDistances; }

      //=====================================================================
      // Step 4 - the reorganised tree T'
      //=====================================================================
      /**
      * Fills an EMPTY Slim-tree with the objects, grouped by Voronoi cell.
      *
      * Only the insertion ORDER changes; the insertion algorithm is the stock
      * one, so any difference in the resulting tree is attributable to the
      * query-induced grouping and to nothing else.
      *
      * Cells are visited largest first, so the biggest cell defines the shape of
      * the top of the tree rather than whichever cell happened to come first.
      * Inside a cell, objects go in ascending distance to the pivot, so the
      * object nearest the pivot is inserted first and tends to become the
      * representative.
      *
      * @param target    An empty tree, with its own page manager.
      * @param objects   The same vector that was passed to AssignCells.
      * @param seedFirst When true, one object per cell is inserted round-robin
      *                  before the rest. This stops the largest cell from
      *                  monopolising the first split, and the difference between
      *                  the two variants is itself a Chapter 4 result.
      * @return Objects inserted.
      */
      long BuildTree(tSlimTree * target, const std::vector<ObjectType *> & objects,
                     bool seedFirst = false){
         if ((target == NULL) || objects.empty()){
            return 0;
         }//end if
         if (CellOf.size() != objects.size()){
            // No assignment: fall back to plain insertion order so the caller
            // still gets a usable tree rather than an empty one.
            for (std::size_t i = 0; i < objects.size(); i++){
               target->Add(objects[i]->Clone());
            }//end for
            return (long) objects.size();
         }//end if

         // Bucket the object indices by cell, each bucket sorted by distance to
         // its pivot.
         std::vector<std::vector<std::size_t> > buckets(Pivots.size());
         for (std::size_t i = 0; i < objects.size(); i++){
            if (CellOf[i] >= 0){
               buckets[CellOf[i]].push_back(i);
            }//end if
         }//end for
         for (std::size_t c = 0; c < buckets.size(); c++){
            SortByDistance(buckets[c]);
         }//end for

         // Largest cell first.
         std::vector<std::size_t> order(buckets.size());
         for (std::size_t c = 0; c < buckets.size(); c++){
            order[c] = c;
         }//end for
         std::sort(order.begin(), order.end(),
                   BucketSizeGreater(buckets));

         long inserted = 0;

         if (seedFirst){
            for (std::size_t c = 0; c < order.size(); c++){
               std::vector<std::size_t> & b = buckets[order[c]];
               if (!b.empty()){
                  target->Add(objects[b.front()]->Clone());
                  inserted++;
               }//end if
            }//end for
         }//end if

         for (std::size_t c = 0; c < order.size(); c++){
            std::vector<std::size_t> & b = buckets[order[c]];
            for (std::size_t i = (seedFirst && !b.empty()) ? 1 : 0; i < b.size(); i++){
               target->Add(objects[b[i]]->Clone());
               inserted++;
            }//end for
         }//end for

         return inserted;
      }//end BuildTree

      //=====================================================================
      // Section 4.4.2 - the navigation graph
      //=====================================================================
      /**
      * Writes the weighted navigation graph as CSV.
      *
      * The vertices are the indexed objects and the query centres; the weight of
      * an object-to-pivot edge is d(x, pi(x)), which AssignCells already
      * computed, so exporting is free.
      *
      * This is a use-oriented navigation LAYER over the metric tree, not a
      * replacement for it: no approximate search is implemented here, and the
      * tree remains solely responsible for coverage and for exactness. The
      * contrast with HNSW is conceptual - there the edges follow insertion
      * order, that is, the structure of the DATA; here they follow the position
      * of the QUERIES.
      *
      * @param os     Destination.
      * @param header Whether to write the CSV header line.
      * @return Edges written.
      */
      long ExportNavigationGraph(std::ostream & os, bool header = true) const {
         if (header){
            os << "src_type,src_id,dst_type,dst_id,weight\n";
         }//end if
         long edges = 0;
         for (std::size_t i = 0; i < CellOf.size(); i++){
            if (CellOf[i] >= 0){
               os << "object," << i << ",pivot," << CellOf[i] << ","
                  << DistToPivot[i] << "\n";
               edges++;
            }//end if
         }//end for
         return edges;
      }//end ExportNavigationGraph

   private:

      stPlainDiskPageManager * QueryPageManager;
      tSlimTree * QueryTree;
      /** Scratch file backing T_Q; removed by Clear(). */
      std::string QueryIndexPath;
      /** Own evaluator: its count IS the Etapa 2 distance cost. */
      EvaluatorType Evaluator;

      std::vector<ObjectType *> Pivots;
      /** pi(x) per object, as an index into Pivots; -1 when unassigned. */
      std::vector<int> CellOf;
      /** d(x, pi(x)) per object - the weights of section 4.4.2. */
      std::vector<double> DistToPivot;

      long AssignDistances;
      long PivotDistances;

      /** Orders bucket indices by descending bucket size. */
      struct BucketSizeGreater{
         const std::vector<std::vector<std::size_t> > & Buckets;
         BucketSizeGreater(const std::vector<std::vector<std::size_t> > & b)
               : Buckets(b) {}
         bool operator()(std::size_t a, std::size_t b) const {
            if (Buckets[a].size() != Buckets[b].size()){
               return Buckets[a].size() > Buckets[b].size();
            }//end if
            return a < b;   // deterministic tie-break
         }//end operator()
      };//end BucketSizeGreater

      /** Sorts object indices by ascending distance to their pivot. */
      void SortByDistance(std::vector<std::size_t> & indices){
         DistanceLess less(DistToPivot);
         std::sort(indices.begin(), indices.end(), less);
      }//end SortByDistance

      struct DistanceLess{
         const std::vector<double> & D;
         DistanceLess(const std::vector<double> & d) : D(d) {}
         bool operator()(std::size_t a, std::size_t b) const {
            if (D[a] != D[b]){
               return D[a] < D[b];
            }//end if
            return a < b;   // deterministic tie-break
         }//end operator()
      };//end DistanceLess

      /**
      * Walks T_Q collecting representatives, read-only, through its own page
      * manager.
      */
      void CollectPivots(u_int32_t pageID, tPivotMode mode){
         stPage * page = QueryPageManager->GetPage(pageID);
         if (page == NULL){
            return;
         }//end if
         stSlimNode * node = stSlimNode::CreateNode(page);

         if (node->GetNodeType() == stSlimNode::INDEX){
            stSlimIndexNode * indexNode = (stSlimIndexNode *) node;
            const u_int32_t count = indexNode->GetNumberOfEntries();

            if (mode == pmNODE_REPS){
               for (u_int32_t i = 0; i < count; i++){
                  ObjectType * rep = new ObjectType();
                  rep->Unserialize(indexNode->GetObject(i),
                                   indexNode->GetObjectSize(i));
                  Pivots.push_back(rep);
               }//end for
            }//end if

            std::vector<u_int32_t> children(count);
            for (u_int32_t i = 0; i < count; i++){
               children[i] = indexNode->GetIndexEntry(i).PageID;
            }//end for
            delete node;
            QueryPageManager->ReleasePage(page);

            // The finer modes need the leaves, so keep descending.
            if (mode != pmNODE_REPS){
               for (u_int32_t i = 0; i < count; i++){
                  CollectPivots(children[i], mode);
               }//end for
            }//end if
            return;
         }//end if

         stSlimLeafNode * leafNode = (stSlimLeafNode *) node;
         const u_int32_t count = leafNode->GetNumberOfEntries();
         if (mode == pmLEAF_REPS){
            const int rep = leafNode->GetRepresentativeEntry();
            if (rep >= 0){
               ObjectType * obj = new ObjectType();
               obj->Unserialize(leafNode->GetObject((u_int32_t) rep),
                                leafNode->GetObjectSize((u_int32_t) rep));
               Pivots.push_back(obj);
            }//end if
         }else if (mode == pmALL_CENTRES){
            for (u_int32_t i = 0; i < count; i++){
               ObjectType * obj = new ObjectType();
               obj->Unserialize(leafNode->GetObject(i), leafNode->GetObjectSize(i));
               Pivots.push_back(obj);
            }//end for
         }//end if

         delete node;
         QueryPageManager->ReleasePage(page);
      }//end CollectPivots

};//end stSlimVoronoiRebuild

#endif //__STSLIMVORONOIREBUILD_H
