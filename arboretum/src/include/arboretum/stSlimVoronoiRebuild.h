/**
* @file
*
* Etapa 2 of the adaptive Slim-tree: query tree plus batch reorganisation.
* Implements section 4.4 of the thesis.
*
* This header is deliberately standalone. It never touches stSlimTree's
* internals; it drives everything through the public API, so it can be dropped
* into a project (or removed from it) without altering the tree itself.
*
* The pipeline is:
*
*   1. Build a SECOND metric tree T_Q over the query centres (section 4.4).
*   2. Take the representatives of T_Q's leaves as PIVOTS P.
*   3. Assign every indexed object to its nearest pivot - the Voronoi
*      partition of Equation 4.9.
*   4. Build a new Slim-tree T' by inserting objects grouped cell by cell.
*
* Why this differs from the VD-tree of Moriyama et al. (2021), which also uses
* Voronoi diagrams: there the pivots come from the DATA. Here they come from the
* distribution of the QUERIES. That is the whole point of section 4.4.
*
* Why it complements Etapa 1: Etapa 1 needs a single region of concentration to
* converge towards (hypothesis H1). When queries are spread over several
* separated regions, Etapa 1 oscillates, whereas the pivot set here simply grows
* to cover every region that was actually queried.
*/

#ifndef __STSLIMVORONOIREBUILD_H
#define __STSLIMVORONOIREBUILD_H

#include <arboretum/stSlimTree.h>
#include <arboretum/stMemoryPageManager.h>

#include <vector>
#include <algorithm>
#include <fstream>
#include <string>

//=============================================================================
// Class template stSlimVoronoiRebuild
//-----------------------------------------------------------------------------
/**
* Batch, query-driven reorganisation of a Slim-tree (thesis section 4.4).
*
* Typical use:
* @code
*   stSlimVoronoiRebuild<TCity, TCityDistanceEvaluator> rebuilder;
*   rebuilder.BuildQueryTree(queryCentres, 1024);   // T_Q
*   rebuilder.ExtractPivots();                      // P
*   rebuilder.AssignCells(objects);                 // Equation 4.9
*   rebuilder.RebuildInto(newTree, objects);        // T'
* @endcode
*
* The ORIGINAL tree is never modified, exactly as section 4.4.1 requires, so
* both organisations can be measured under the same workload.
*/
template <class ObjectType, class EvaluatorType>
class stSlimVoronoiRebuild{
   public:

      typedef stSlimTree<ObjectType, EvaluatorType> tSlimTree;

      stSlimVoronoiRebuild(){
         QueryTree = NULL;
         QueryPageManager = NULL;
         PivotDistanceCalls = 0;
         AssignDistanceCalls = 0;
      }//end stSlimVoronoiRebuild

      ~stSlimVoronoiRebuild(){
         Clear();
      }//end ~stSlimVoronoiRebuild

      /**
      * Releases T_Q, the pivots and the cell assignment.
      */
      void Clear(){
         if (QueryTree != NULL){
            delete QueryTree;
            QueryTree = NULL;
         }//end if
         if (QueryPageManager != NULL){
            delete QueryPageManager;
            QueryPageManager = NULL;
         }//end if
         for (size_t i = 0; i < Pivots.size(); i++){
            delete Pivots[i];
         }//end for
         Pivots.clear();
         CellOf.clear();
         DistanceToPivot.clear();
      }//end Clear

      //=====================================================================
      // Step 1 - the query tree T_Q (section 4.4)
      //=====================================================================
      /**
      * Indexes the query centres in a second, in-memory metric tree.
      *
      * T_Q is small by construction: the number of queries seen in a time
      * window is typically far below the number of indexed objects, which is
      * exactly the argument section 4.4 makes for the low maintenance cost of
      * this structure. An in-memory page manager is therefore appropriate.
      *
      * @param queryCentres The centres of the queries observed so far.
      * @param pageSize     Page size for T_Q.
      */
      void BuildQueryTree(const std::vector<ObjectType *> & queryCentres,
                          u_int32_t pageSize = 1024){
         Clear();
         if (queryCentres.empty()){
            return;
         }//end if

         QueryPageManager = new stMemoryPageManager(pageSize);
         QueryTree = new tSlimTree(QueryPageManager);

         for (size_t i = 0; i < queryCentres.size(); i++){
            QueryTree->Add(queryCentres[i]);
         }//end for
      }//end BuildQueryTree

      /**
      * The query tree itself, for statistics. NULL before BuildQueryTree().
      */
      tSlimTree * GetQueryTree(){
         return QueryTree;
      }//end GetQueryTree

      //=====================================================================
      // Step 2 - pivot extraction (section 4.4.1)
      //=====================================================================
      /**
      * Collects the representatives of T_Q's leaf nodes as the pivot set P.
      *
      * Using leaf representatives rather than every query centre makes the
      * number of pivots follow query DENSITY automatically: a heavily queried
      * region produces more leaves in T_Q, hence more pivots, hence a finer
      * partition exactly where section 4.4.1 says granularity is wanted. A
      * region nobody queries contributes at most one pivot.
      *
      * @return The number of pivots extracted.
      */
      size_t ExtractPivots(){
         for (size_t i = 0; i < Pivots.size(); i++){
            delete Pivots[i];
         }//end for
         Pivots.clear();

         if (QueryTree == NULL){
            return 0;
         }//end if

         // Delegated to the tree itself: the walk needs the root page id,
         // which stSlimTree keeps private.
         QueryTree->CollectLeafRepresentatives(Pivots);

         return Pivots.size();
      }//end ExtractPivots

      /**
      * Overrides pivot extraction with an explicit set, e.g. to use every
      * query centre directly instead of the leaf representatives. Useful as an
      * ablation when reporting section 4.5.
      */
      void SetPivots(const std::vector<ObjectType *> & pivots){
         for (size_t i = 0; i < Pivots.size(); i++){
            delete Pivots[i];
         }//end for
         Pivots.clear();
         for (size_t i = 0; i < pivots.size(); i++){
            Pivots.push_back((ObjectType *) pivots[i]->Clone());
         }//end for
      }//end SetPivots

      size_t GetPivotCount(){
         return Pivots.size();
      }//end GetPivotCount

      ObjectType * GetPivot(size_t i){
         return Pivots[i];
      }//end GetPivot

      //=====================================================================
      // Step 3 - the Voronoi partition (Equation 4.9)
      //=====================================================================
      /**
      * pi(x) = argmin over p in P of d(x, p), for every indexed object.
      *
      * Costs |objects| x |P| distance computations. That is the bulk of the
      * batch reorganisation cost that section 4.5 asks to be measured, so the
      * count is exposed through GetAssignDistanceCalls() rather than hidden.
      *
      * d(x, pi(x)) falls out of this loop for free and is retained: it orders
      * insertions in step 4 and it is the edge weight of the navigation graph
      * of section 4.4.2.
      *
      * @param objects   The objects indexed by the original tree.
      * @param evaluator Distance function. Pass a DEDICATED instance if these
      *                  distances must not pollute the tree's own statistics.
      */
      void AssignCells(const std::vector<ObjectType *> & objects,
                       EvaluatorType & evaluator){
         CellOf.assign(objects.size(), 0);
         DistanceToPivot.assign(objects.size(), 0.0);
         AssignDistanceCalls = 0;

         if (Pivots.empty()){
            return;
         }//end if

         for (size_t i = 0; i < objects.size(); i++){
            size_t bestPivot = 0;
            double bestDist = evaluator.GetDistance(*objects[i], *Pivots[0]);
            AssignDistanceCalls++;

            for (size_t p = 1; p < Pivots.size(); p++){
               const double d = evaluator.GetDistance(*objects[i], *Pivots[p]);
               AssignDistanceCalls++;
               if (d < bestDist){
                  bestDist = d;
                  bestPivot = p;
               }//end if
            }//end for

            CellOf[i] = bestPivot;
            DistanceToPivot[i] = bestDist;
         }//end for
      }//end AssignCells

      long GetAssignDistanceCalls(){
         return AssignDistanceCalls;
      }//end GetAssignDistanceCalls

      /**
      * Voronoi cell of the i-th object, as an index into the pivot set.
      */
      size_t GetCellOf(size_t i){
         return CellOf[i];
      }//end GetCellOf

      /**
      * d(x, pi(x)) for the i-th object.
      */
      double GetDistanceToPivot(size_t i){
         return DistanceToPivot[i];
      }//end GetDistanceToPivot

      //=====================================================================
      // Step 4 - build T' (section 4.4.1)
      //=====================================================================
      /**
      * Inserts every object into a fresh tree, grouped by Voronoi cell.
      *
      * The insertion ALGORITHM is untouched: this only chooses the insertion
      * ORDER. Because ChooseSubTree() picks the subtree that already covers the
      * new object (or the nearest representative otherwise), feeding it a run
      * of objects from the same cell makes them land in the same leaves. That
      * is precisely the wording of section 4.4.1: "objetos associados a mesma
      * celula tendam a permanecer na mesma subarvore".
      *
      * Within a cell, objects are ordered by increasing d(x, pi(x)). The object
      * closest to the pivot is inserted first and therefore tends to become the
      * representative of the leaf, which keeps the covering radii tight.
      *
      * The query pivots orient the organisation but are NOT themselves indexed
      * (section 4.4.1, final sentence).
      *
      * @param target  An empty Slim-tree to fill.
      * @param objects The same vector passed to AssignCells().
      */
      void RebuildInto(tSlimTree * target,
                       const std::vector<ObjectType *> & objects){
         if (Pivots.empty() || CellOf.size() != objects.size()){
            // No usable partition: fall back to plain insertion order so the
            // caller still ends up with a valid tree.
            for (size_t i = 0; i < objects.size(); i++){
               target->Add(objects[i]);
            }//end for
            return;
         }//end if

         // (cell, distance to pivot, object index), sorted lexicographically.
         std::vector<std::pair<std::pair<size_t, double>, size_t> > order;
         order.reserve(objects.size());
         for (size_t i = 0; i < objects.size(); i++){
            order.push_back(std::make_pair(
                  std::make_pair(CellOf[i], DistanceToPivot[i]), i));
         }//end for
         std::sort(order.begin(), order.end());

         for (size_t i = 0; i < order.size(); i++){
            target->Add(objects[order[i].second]);
         }//end for
      }//end RebuildInto

      //=====================================================================
      // Section 4.4.2 - navigation graph derived from the distances
      //=====================================================================
      /**
      * Dumps the weighted edge list (object, pivot, distance) produced as a
      * by-product of AssignCells().
      *
      * Scope note: section 4.4.2 presents this graph as an auxiliary
      * navigation layer, and the four configurations evaluated in section 4.5
      * do not include it. Exporting the edges is therefore all that is
      * implemented here - no search runs over this graph; the metric tree
      * remains solely responsible for coverage and for exact answers.
      *
      * @param path      Destination CSV file.
      * @param objects   The objects passed to AssignCells().
      * @param nameOf    Optional callback turning an object into a label.
      * @return true on success.
      */
      bool ExportNavigationGraph(const std::string & path,
                                 const std::vector<ObjectType *> & objects){
         if (CellOf.size() != objects.size()){
            return false;
         }//end if

         std::ofstream os(path.c_str());
         if (!os){
            return false;
         }//end if

         os << "object_index,pivot_index,distance\n";
         os.setf(std::ios::fixed);
         os.precision(6);
         for (size_t i = 0; i < objects.size(); i++){
            os << i << ',' << CellOf[i] << ',' << DistanceToPivot[i] << '\n';
         }//end for
         return true;
      }//end ExportNavigationGraph

      /**
      * Number of objects landing in each Voronoi cell. A very uneven histogram
      * means the query centres were themselves very uneven, which is the
      * signal section 4.4.1 uses to argue Etapa 2 suits dispersed workloads.
      */
      std::vector<size_t> GetCellSizes(){
         std::vector<size_t> sizes(Pivots.size(), 0);
         for (size_t i = 0; i < CellOf.size(); i++){
            sizes[CellOf[i]]++;
         }//end for
         return sizes;
      }//end GetCellSizes

   private:
      /** T_Q, the tree over query centres. */
      tSlimTree * QueryTree;

      /** In-memory pages backing T_Q. */
      stMemoryPageManager * QueryPageManager;

      /** The pivot set P extracted from T_Q. */
      std::vector<ObjectType *> Pivots;

      /** CellOf[i] = index into Pivots of the pivot nearest to object i. */
      std::vector<size_t> CellOf;

      /** DistanceToPivot[i] = d(object_i, pi(object_i)). */
      std::vector<double> DistanceToPivot;

      long PivotDistanceCalls;
      long AssignDistanceCalls;

};//end stSlimVoronoiRebuild

#endif //__STSLIMVORONOIREBUILD_H
