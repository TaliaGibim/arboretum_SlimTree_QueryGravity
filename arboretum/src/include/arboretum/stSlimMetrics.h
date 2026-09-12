/**
* @file
*
* Structural metrics of a Slim-tree, for the Chapter 4 evaluation (section 4.5).
*
* Section 4.5 asks for coverage radii, OVERLAP, node occupancy and the fat
* factor. The fat factors come from stSlimTree::GetFatFactor() and
* GetRelativeFatFactor(). Everything else comes from here, because
* stTreeInfoResult exposes the fat factors but keeps the raw intersection counts
* protected, and it offers nothing at all about per-leaf occupancy.
*
* This header is deliberately standalone and READ-ONLY: it walks the tree
* through the page manager and never writes a page. It touches no private member
* of stSlimTree; the only thing it needs from the tree is the root page id,
* obtained through the public GetRootPageID().
*
* TWO ACCOUNTING RULES - both matter for the numbers reported in the thesis:
*
* 1. The walk uses its OWN EvaluatorType instance, so the distances it computes
*    for the overlap measure are NOT added to the tree official distance count.
*    They are reported separately as Result::WalkDistances.
*
* 2. The walk DOES go through the page manager, so it DOES increment its read
*    counter; there is no way around that and no setter to restore it. Collect()
*    must therefore run AFTER the current batch CSV row has been written and
*    BEFORE the next pm->ResetStatistics(). Result::WalkPageReads records how
*    many reads the walk itself caused, so the pollution is at least auditable.
*/

#ifndef __STSLIMMETRICS_H
#define __STSLIMMETRICS_H

#include <arboretum/stSlimNode.h>
#include <arboretum/stPageManager.h>

#include <vector>
#include <cstddef>

//=============================================================================
// Class template stSlimMetrics
//-----------------------------------------------------------------------------
/**
* Read-only structural collector for a Slim-tree.
*
* @param ObjectType    The indexed object type.
* @param EvaluatorType The metric evaluator type. An instance is created
*                      internally so the tree own counters stay untouched.
*/
template <class ObjectType, class EvaluatorType>
class stSlimMetrics{
   public:

      /**
      * Everything the walk measured.
      */
      struct Result{
         /** Node census. */
         long IndexNodes;
         long LeafNodes;
         long TotalNodes;
         /** Objects actually found in leaves (should equal GetNumberOfObjects). */
         long Objects;
         /** Deepest level reached, root = 0. Height = MaxLevel + 1. */
         int MaxLevel;
         /** Nodes per level, index 0 = root level. */
         std::vector<long> NodesPerLevel;

         /** Entries per leaf. */
         long MinLeafEntries;
         long MaxLeafEntries;
         double MeanLeafEntries;

         /** Used bytes divided by page bytes, per leaf. */
         double MinLeafFill;
         double MaxLeafFill;
         double MeanLeafFill;

         /** Leaf covering radii. */
         double MinLeafRadius;
         double MaxLeafRadius;
         double MeanLeafRadius;

         /**
         * Overlap, in the operational sense of section 2.3.2.6.3: two sibling
         * regions overlap when their balls intersect, that is, when
         *
         *     d(O_i, O_j) < r_i + r_j
         *
         * OverlappingPairs counts such sibling pairs, SiblingPairs counts all
         * pairs examined, and OverlapRatio is the quotient. MeanAbsOverlap
         * averages max(0, r_i + r_j - d(O_i,O_j)) over ALL sibling pairs, which
         * measures how deeply the balls interpenetrate rather than merely
         * whether they touch.
         */
         long SiblingPairs;
         long OverlappingPairs;
         double OverlapRatio;
         double MeanAbsOverlap;

         /** Cost of this walk, kept out of the tree official statistics. */
         long WalkDistances;
         long WalkPageReads;

         Result(){
            IndexNodes = 0;
            LeafNodes = 0;
            TotalNodes = 0;
            Objects = 0;
            MaxLevel = 0;
            MinLeafEntries = 0;
            MaxLeafEntries = 0;
            MeanLeafEntries = 0.0;
            MinLeafFill = 0.0;
            MaxLeafFill = 0.0;
            MeanLeafFill = 0.0;
            MinLeafRadius = 0.0;
            MaxLeafRadius = 0.0;
            MeanLeafRadius = 0.0;
            SiblingPairs = 0;
            OverlappingPairs = 0;
            OverlapRatio = 0.0;
            MeanAbsOverlap = 0.0;
            WalkDistances = 0;
            WalkPageReads = 0;
         }//end Result
      };//end Result

      /**
      * Walks the whole tree and returns its structural metrics.
      *
      * @param pageManager The page manager backing the tree.
      * @param rootPageID  Root page id, from stSlimTree::GetRootPageID().
      * @return The collected metrics. All fields are zero for an empty tree.
      */
      static Result Collect(stPageManager * pageManager, u_int32_t rootPageID){
         Result result;
         if ((pageManager == NULL) || (rootPageID == 0)){
            return result;
         }//end if

         EvaluatorType evaluator;
         Accumulator acc;

         Walk(pageManager, evaluator, rootPageID, 0, result, acc);

         if (result.LeafNodes > 0){
            result.MeanLeafEntries = acc.SumEntries / (double) result.LeafNodes;
            result.MeanLeafFill    = acc.SumFill    / (double) result.LeafNodes;
            result.MeanLeafRadius  = acc.SumRadius  / (double) result.LeafNodes;
            result.MinLeafEntries  = acc.MinEntries;
            result.MaxLeafEntries  = acc.MaxEntries;
            result.MinLeafFill     = acc.MinFill;
            result.MaxLeafFill     = acc.MaxFill;
            result.MinLeafRadius   = acc.MinRadius;
            result.MaxLeafRadius   = acc.MaxRadius;
         }//end if

         if (result.SiblingPairs > 0){
            result.OverlapRatio =
                  (double) result.OverlappingPairs / (double) result.SiblingPairs;
            result.MeanAbsOverlap = acc.SumAbsOverlap / (double) result.SiblingPairs;
         }//end if

         result.TotalNodes = result.IndexNodes + result.LeafNodes;
         return result;
      }//end Collect

   private:

      /**
      * Running sums, kept out of Result so that Result stays a plain report.
      */
      struct Accumulator{
         double SumEntries;
         double SumFill;
         double SumRadius;
         double SumAbsOverlap;
         long MinEntries;
         long MaxEntries;
         double MinFill;
         double MaxFill;
         double MinRadius;
         double MaxRadius;
         bool First;

         Accumulator(){
            SumEntries = 0.0;
            SumFill = 0.0;
            SumRadius = 0.0;
            SumAbsOverlap = 0.0;
            MinEntries = 0;
            MaxEntries = 0;
            MinFill = 0.0;
            MaxFill = 0.0;
            MinRadius = 0.0;
            MaxRadius = 0.0;
            First = true;
         }//end Accumulator
      };//end Accumulator

      /**
      * Depth-first walk. The page of an index node is released BEFORE recursing
      * into its children, so at most one page per LEVEL is held, not one per
      * node.
      */
      static void Walk(stPageManager * pageManager, EvaluatorType & evaluator,
                       u_int32_t pageID, int level, Result & result,
                       Accumulator & acc){

         stPage * page = pageManager->GetPage(pageID);
         if (page == NULL){
            return;
         }//end if
         result.WalkPageReads++;

         stSlimNode * node = stSlimNode::CreateNode(page);

         if (level > result.MaxLevel){
            result.MaxLevel = level;
         }//end if
         while ((int) result.NodesPerLevel.size() <= level){
            result.NodesPerLevel.push_back(0);
         }//end while
         result.NodesPerLevel[level]++;

         if (node->GetNodeType() == stSlimNode::INDEX){
            stSlimIndexNode * indexNode = (stSlimIndexNode *) node;
            const u_int32_t count = indexNode->GetNumberOfEntries();
            result.IndexNodes++;

            // Sibling overlap among this node entries (section 2.3.2.6.3).
            // Unserialising into a local vector first keeps the number of
            // Unserialize calls at O(count) instead of O(count^2).
            std::vector<ObjectType> reps(count);
            for (u_int32_t i = 0; i < count; i++){
               reps[i].Unserialize(indexNode->GetObject(i),
                                   indexNode->GetObjectSize(i));
            }//end for

            for (u_int32_t i = 0; i < count; i++){
               const double ri = indexNode->GetIndexEntry(i).Radius;
               for (u_int32_t j = i + 1; j < count; j++){
                  const double rj = indexNode->GetIndexEntry(j).Radius;
                  const double d = evaluator.GetDistance(reps[i], reps[j]);
                  result.WalkDistances++;
                  result.SiblingPairs++;
                  const double slack = (ri + rj) - d;
                  if (slack > 0.0){
                     result.OverlappingPairs++;
                     acc.SumAbsOverlap += slack;
                  }//end if
               }//end for
            }//end for

            std::vector<u_int32_t> children(count);
            for (u_int32_t i = 0; i < count; i++){
               children[i] = indexNode->GetIndexEntry(i).PageID;
            }//end for

            delete node;
            pageManager->ReleasePage(page);

            for (u_int32_t i = 0; i < count; i++){
               Walk(pageManager, evaluator, children[i], level + 1, result, acc);
            }//end for
            return;
         }//end if

         // Leaf.
         stSlimLeafNode * leafNode = (stSlimLeafNode *) node;
         const long entries = (long) leafNode->GetNumberOfEntries();
         const double radius = leafNode->GetMinimumRadius();
         const double pageSize = (double) page->GetPageSize();
         const double fill = (pageSize > 0.0)
               ? (pageSize - (double) leafNode->GetFree()) / pageSize
               : 0.0;

         result.LeafNodes++;
         result.Objects += entries;

         acc.SumEntries += (double) entries;
         acc.SumFill += fill;
         acc.SumRadius += radius;
         if (acc.First){
            acc.MinEntries = acc.MaxEntries = entries;
            acc.MinFill = acc.MaxFill = fill;
            acc.MinRadius = acc.MaxRadius = radius;
            acc.First = false;
         }else{
            if (entries < acc.MinEntries) acc.MinEntries = entries;
            if (entries > acc.MaxEntries) acc.MaxEntries = entries;
            if (fill < acc.MinFill) acc.MinFill = fill;
            if (fill > acc.MaxFill) acc.MaxFill = fill;
            if (radius < acc.MinRadius) acc.MinRadius = radius;
            if (radius > acc.MaxRadius) acc.MaxRadius = radius;
         }//end if

         delete node;
         pageManager->ReleasePage(page);
      }//end Walk

};//end stSlimMetrics

#endif //__STSLIMMETRICS_H
