/**
* @file
*
* Etapa 1 of the adaptive Slim-tree: query-guided relocation (thesis 4.3).
*
* WHAT IT DOES
* ------------
* After a query has been answered, the objects that sit in a zone covered by
* more than one of the sibling nodes the query visited ("cobertura ambigua",
* Definition 2) are moved to whichever of those siblings has its representative
* CLOSER TO THE QUERY CENTRE. The four conditions are Equations 4.3 to 4.6:
*
*   (4.3)  d(x, O_j) <= r_j        the destination ALREADY covers x
*   (4.4)  d(q, O_j) <  d(q, O_i)  resolve the ambiguity towards the query
*   (4.5)  ocup(N_j) + |x| <= C    the destination page has room
*   (4.6)  x != O_i                never move a representative
*
* and then Equation 4.7 recomputes the source radius, which can only shrink:
*
*   (4.7)  r'_i = max over the remaining y of d(y, O_i),  with r'_i <= r_i.
*
* Condition 4.3 is the load-bearing one. Because the destination already covers
* x, its radius NEVER grows, the coverage invariant of Definition 1 is
* preserved, and pruning can only get stronger. That is the exact opposite of
* the abandoned Query Gravity, which relaxed this very bound.
*
* WHY IT LIVES OUTSIDE stSlimTree
* -------------------------------
* Relocation needs the page manager and the page ids of a parent and two of its
* children. The page ids come from the traversal trace, so the root is never
* needed, and stSlimIndexNode / stSlimLeafNode are public. Nothing private to
* stSlimTree is required, so none of this belongs inside it.
*
* It is safe to mutate pages behind the tree's back here because relocation
* NEVER allocates or disposes a page and never touches Root, Height, NodeCount
* or ObjectCount. The page graph is fixed; only page contents change.
*
* HOW IT STAYS CHEAP
* ------------------
* Equation 4.4 costs NOTHING: both d(q,O_i) and d(q,O_j) were already computed
* by the traversal and recorded in the trace, so sorting the visited siblings by
* that distance decides 4.4 for every pair at once.
*
* Equation 4.3 is filtered by the triangle inequality before it is evaluated.
* Every leaf entry already stores d(x, O_i), so with a single d(O_i, O_j) per
* NODE PAIR,
*
*     d(x, O_j) >= |d(x, O_i) - d(O_i, O_j)|
*
* rejects a candidate with ZERO distance computations whenever that lower bound
* already exceeds r_j. Only survivors pay for an exact d(x, O_j).
*
* ORDER OF WORK
* -------------
* Candidates are moved FARTHEST-FROM-O_i FIRST. Since r_i is the maximum stored
* distance, that is the only order in which Equation 4.7 can actually shrink the
* source radius - the same reasoning behind PopObject() in the classic
* Slim-Down, which keeps the comparison in section 4.5 honest.
*
* This forces a two-stage implementation. stSlimLeafNode::RemoveEntry compacts
* the entry array, so indices shift; and the farthest-first order is unrelated
* to index order. Stage A therefore scans without mutating and collects the
* accepted candidates; stage B inserts them into the destination and only then
* removes them from the source in DESCENDING index order, which is the one
* order that compaction cannot invalidate.
*
* @warning Do NOT reach for stSlimMemLeafNode to "simplify" this. Its
* constructor calls RemoveAll() on the source leaf and ReleaseNode() re-sorts
* the entries, so merely inspecting a node through it rewrites and reorders the
* page even when nothing moves. That would both inflate the write count and
* change the structure being compared against. This code works on the raw
* stSlimLeafNode for that reason.
*/

#ifndef __STSLIMETAPA1_H
#define __STSLIMETAPA1_H

#include <arboretum/stSlimNode.h>
#include <arboretum/stPageManager.h>
#include <arboretum/stSlimQueryTrace.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#if ST_SLIM_TRACE

//=============================================================================
// Class template stSlimEtapa1
//-----------------------------------------------------------------------------
/**
* Query-guided relocation engine (thesis section 4.3).
*
* @param ObjectType    The indexed object type.
* @param EvaluatorType The metric evaluator. An instance is owned internally, so
*                      the distances relocation spends are counted separately
*                      from the ones the queries spend - section 4.5 reports
*                      them apart, and the evaluator has a single global
*                      counter that could not otherwise tell them apart.
*/
template <class ObjectType, class EvaluatorType>
class stSlimEtapa1{
   public:

      //=====================================================================
      // Configuration
      //=====================================================================
      /**
      * Tunables. Every default is NEUTRAL: it implements Equations 4.3 to 4.7
      * as written, with no extra damping, so that the raw behaviour - including
      * the oscillation hypothesis H4 warns about - is what gets measured first.
      */
      struct Config{
         /**
         * Upper bound on relocations after a single query. Caps the worst-case
         * latency a query can absorb. H4 predicts the rate decays anyway once
         * the tree stabilises.
         */
         int MaxMovesPerQuery;

         /**
         * Margin on Equation 4.4, applied as d(q,O_j) < d(q,O_i) - MinGain.
         * Above zero it demands a strict improvement.
         */
         double MinGain;

         /**
         * Multiplicative form of the same margin: d(q,O_j) < Alpha * d(q,O_i).
         * 1.0 is neutral. Below 1.0 it damps the ping-pong of H4 by requiring
         * the destination to be proportionally, not just marginally, closer.
         */
         double HysteresisAlpha;

         /**
         * Refuse to move the same object again within this many queries. Zero
         * disables the cooldown, which is the default so that the reverse
         * relocation rate can be OBSERVED before it is engineered away.
         */
         long CooldownQueries;

         /**
         * Never let a source leaf fall below this many entries. Keeps Etapa 1
         * strictly page-count preserving - unlike Slim-Down, which empties and
         * disposes leaves - and stops occupancy statistics from being wrecked
         * by leaves drained down to their representative.
         */
         long MinLeafEntries;

         /**
         * Accept a move only when it actually shrinks the source radius. This
         * is the operational reading of Equation 4.8: for k-NN, relocation pays
         * off only when the tighter radius lets some node be pruned later.
         */
         bool RequireShrink;

         Config(){
            MaxMovesPerQuery = 8;
            MinGain = 0.0;
            HysteresisAlpha = 1.0;
            CooldownQueries = 0;
            MinLeafEntries = 2;
            RequireShrink = false;
         }//end Config
      };//end Config

      //=====================================================================
      // Statistics
      //=====================================================================
      /**
      * Everything section 4.5 asks about the adaptation, plus the rejection
      * breakdown that explains WHY moves did or did not happen.
      */
      struct Stats{
         /** Queries handed to ApplyAfterQuery. */
         long QueriesSeen;
         /** Of those, how many produced at least one move. */
         long QueriesWithWork;
         /** (source, destination) sibling pairs examined. */
         long PairsConsidered;
         /** Pairs skipped because a child was not a leaf (see AllowIndexLevel). */
         long PairsSkippedNotLeaf;

         /** Objects actually moved. */
         long ObjectsRelocated;
         /** Moves back to a node the object had been moved away from (H4). */
         long ReverseRelocations;
         /** Total radius shrink achieved, summed over moves (Eq. 4.7). */
         double RadiusShrinkTotal;

         /**
         * Candidates rejected by the triangle-inequality lower bound, i.e. at
         * ZERO distance cost. This is the number that justifies the claim that
         * Etapa 1 is affordable.
         */
         long SkippedByBound;
         /** Rejected by the exact test of Equation 4.3. */
         long RejectedByCoverage;
         /** Rejected by Equation 4.5 - the destination page was full. */
         long RejectedByCapacity;
         /** Rejected by Equation 4.6 - it was the representative. */
         long RejectedByRepresentative;
         /** Rejected because d(x,O_j) was 0 and would forge a representative. */
         long RejectedByDuplicate;
         /** Rejected to keep the source above MinLeafEntries. */
         long RejectedByUnderflow;
         /** Rejected by the Equation 4.4 margin. */
         long RejectedByGain;
         /** Rejected by the cooldown, which is off unless CooldownQueries > 0. */
         long RejectedByCooldown;
         /** Rejected by RequireShrink (Equation 4.8 gate). */
         long RejectedByNoShrink;

         /**
         * MUST STAY ZERO. Equation 4.3 guarantees the destination radius cannot
         * grow; a non-zero value here refutes H2 and means the implementation
         * is wrong.
         */
         long DestinationRadiusGrowthEvents;

         /** Cost of the adaptation, kept apart from the query cost. */
         long Distances;
         long PageReads;
         long PageWrites;

         /**
         * The two kinds of distance, split because they answer different
         * questions. PairDistances is the fixed d(O_i,O_j) overhead, one per
         * sibling pair. CandidateDistances is the variable part: the exact
         * Equation 4.3 test, paid only by candidates the free triangle bound
         * could not already reject. Distances is their sum.
         *
         * SkippedByBound / (SkippedByBound + CandidateDistances) is the
         * fraction of candidates decided at ZERO distance cost - the honest
         * form of that claim, which comparing against Distances would overstate
         * by folding in the per-pair overhead.
         */
         long PairDistances;
         long CandidateDistances;

         /** Moves per query, in order - the H4 decay curve. */
         std::vector<long> MovesPerQuery;

         Stats(){
            Reset();
         }//end Stats

         void Reset(){
            QueriesSeen = 0;
            QueriesWithWork = 0;
            PairsConsidered = 0;
            PairsSkippedNotLeaf = 0;
            ObjectsRelocated = 0;
            ReverseRelocations = 0;
            RadiusShrinkTotal = 0.0;
            SkippedByBound = 0;
            RejectedByCoverage = 0;
            RejectedByCapacity = 0;
            RejectedByRepresentative = 0;
            RejectedByDuplicate = 0;
            RejectedByUnderflow = 0;
            RejectedByGain = 0;
            RejectedByCooldown = 0;
            RejectedByNoShrink = 0;
            DestinationRadiusGrowthEvents = 0;
            Distances = 0;
            PageReads = 0;
            PageWrites = 0;
            PairDistances = 0;
            CandidateDistances = 0;
            MovesPerQuery.clear();
         }//end Reset
      };//end Stats

      //=====================================================================
      // Construction
      //=====================================================================
      /**
      * @param pageManager The page manager backing the tree being adapted.
      * @param config      Tunables; the default is the thesis as written.
      */
      stSlimEtapa1(stPageManager * pageManager, const Config & config = Config()){
         PageManager = pageManager;
         Cfg = config;
      }//end stSlimEtapa1

      const Stats & GetStats() const { return St; }
      const Config & GetConfig() const { return Cfg; }
      void SetConfig(const Config & config){ Cfg = config; }
      void ResetStats(){ St.Reset(); }

      /** Forgets the per-object move history used by the cooldown and by H4. */
      void ResetHistory(){
         LastSource.clear();
         LastMoveQuery.clear();
      }//end ResetHistory

      //=====================================================================
      // The pass itself
      //=====================================================================
      /**
      * Relocates objects according to what the given query traversal saw.
      *
      * @param trace The trace of the query that has just been answered.
      * @return The number of objects moved.
      */
      long ApplyAfterQuery(const stSlimTrace & trace){
         St.QueriesSeen++;
         long movedHere = 0;

         if (PageManager != NULL){
            int movesLeft = Cfg.MaxMovesPerQuery;

            // Only edges whose child was really READ describe a comparison the
            // query actually made. In k-NN an edge can be queued and then never
            // expanded because tau shrank; reasoning about such a sibling would
            // be reasoning about a node the query never looked at.
            std::map<u_int32_t, std::vector<stSlimVisit> > byParent;
            for (std::size_t i = 0; i < trace.Visits.size(); i++){
               if (trace.Visits[i].Expanded){
                  byParent[trace.Visits[i].Parent].push_back(trace.Visits[i]);
               }//end if
            }//end for

            for (typename std::map<u_int32_t, std::vector<stSlimVisit> >::iterator
                     it = byParent.begin();
                 (it != byParent.end()) && (movesLeft > 0); ++it){
               if (it->second.size() >= 2){
                  movedHere += ProcessParent(it->first, it->second, movesLeft);
               }//end if
            }//end for
         }//end if

         if (movedHere > 0){
            St.QueriesWithWork++;
         }//end if
         St.MovesPerQuery.push_back(movedHere);
         return movedHere;
      }//end ApplyAfterQuery

   private:

      stPageManager * PageManager;
      Config Cfg;
      Stats St;
      /** Own evaluator: its count IS the adaptation distance cost. */
      EvaluatorType Evaluator;

      /** Serialized object -> page it was last moved away from (H4). */
      std::map<std::string, u_int32_t> LastSource;
      /** Serialized object -> query index at its last move (cooldown). */
      std::map<std::string, long> LastMoveQuery;

      /** A candidate that already passed Equations 4.3 and 4.6. */
      struct Candidate{
         u_int32_t Index;     // position in the source leaf, before any removal
         double DistToSrcRep; // d(x, O_i), read from the stored entry
         double DistToDstRep; // d(x, O_j), paid for exactly once
         ObjectType Object;   // a copy, so no pointer into the page survives
      };

      static bool FarthestFirst(const Candidate & a, const Candidate & b){
         return a.DistToSrcRep > b.DistToSrcRep;
      }//end FarthestFirst

      /** Serialized form of an object, used as an identity key. */
      std::string KeyOf(ObjectType & object){
         return std::string((const char *) object.Serialize(),
                            (std::size_t) object.GetSerializedSize());
      }//end KeyOf

      //---------------------------------------------------------------------
      /**
      * Handles all sibling pairs under one parent.
      *
      * The visited children are sorted by d(q, O): the nearest becomes the
      * destination N_j and every other visited child is a source N_i, which
      * makes Equation 4.4 true by construction for every pair at no cost.
      */
      long ProcessParent(u_int32_t parentPageID,
                         std::vector<stSlimVisit> group, int & movesLeft){

         std::sort(group.begin(), group.end(), ByDistToQuery);

         stPage * parentPage = PageManager->GetPage(parentPageID);
         if (parentPage == NULL){
            return 0;
         }//end if
         St.PageReads++;

         stSlimNode * parentNode = stSlimNode::CreateNode(parentPage);
         if (parentNode->GetNodeType() != stSlimNode::INDEX){
            delete parentNode;
            PageManager->ReleasePage(parentPage);
            return 0;
         }//end if
         stSlimIndexNode * parent = (stSlimIndexNode *) parentNode;

         const stSlimVisit destination = group[0];
         long moved = 0;
         bool parentChanged = false;

         for (std::size_t s = 1; (s < group.size()) && (movesLeft > 0); s++){
            // Equation 4.4, with both margins. Neutral defaults make this the
            // plain d(q,O_j) < d(q,O_i) of the thesis.
            const double dq = destination.DistToQuery;
            const double di = group[s].DistToQuery;
            if (!((dq < di - Cfg.MinGain) && (dq < Cfg.HysteresisAlpha * di))){
               St.RejectedByGain++;
               continue;
            }//end if

            // Positions are re-resolved by page id rather than trusting the
            // index the trace recorded: a previous pair may already have
            // rewritten this parent.
            int srcIdx = FindEntry(parent, group[s].Child);
            int dstIdx = FindEntry(parent, destination.Child);
            if ((srcIdx < 0) || (dstIdx < 0) || (srcIdx == dstIdx)){
               continue;
            }//end if

            St.PairsConsidered++;
            if (RelocatePair(parent, (u_int32_t) srcIdx, (u_int32_t) dstIdx,
                             movesLeft, moved)){
               parentChanged = true;
            }//end if
         }//end for

         if (parentChanged){
            // The parent's own radius may now be smaller. Ancestors are
            // deliberately left alone: N_i and N_j share every ancestor, so
            // every object stays inside exactly the same ancestor balls and
            // Definition 1 still holds. An ancestor radius that is merely
            // LARGER than necessary only widens a pruning test - it can cost an
            // extra visit but can never lose a result.
            PageManager->WritePage(parentPage);
            St.PageWrites++;
         }//end if

         delete parentNode;
         PageManager->ReleasePage(parentPage);
         return moved;
      }//end ProcessParent

      static bool ByDistToQuery(const stSlimVisit & a, const stSlimVisit & b){
         return a.DistToQuery < b.DistToQuery;
      }//end ByDistToQuery

      static int FindEntry(stSlimIndexNode * parent, u_int32_t childPageID){
         for (u_int32_t e = 0; e < parent->GetNumberOfEntries(); e++){
            if (parent->GetIndexEntry(e).PageID == childPageID){
               return (int) e;
            }//end if
         }//end for
         return -1;
      }//end FindEntry

      //---------------------------------------------------------------------
      /**
      * Moves what it can from one leaf into a sibling leaf.
      *
      * @return true when pages were modified and the parent must be written.
      */
      bool RelocatePair(stSlimIndexNode * parent, u_int32_t srcIdx,
                        u_int32_t dstIdx, int & movesLeft, long & moved){

         const u_int32_t srcPageID = parent->GetIndexEntry(srcIdx).PageID;
         const u_int32_t dstPageID = parent->GetIndexEntry(dstIdx).PageID;
         if ((srcPageID == 0) || (dstPageID == 0) || (srcPageID == dstPageID)){
            return false;
         }//end if

         const double dstRadius = parent->GetIndexEntry(dstIdx).Radius;

         // Both representatives are duplicated in the parent, so reading them
         // costs no page access.
         ObjectType srcRep;
         ObjectType dstRep;
         srcRep.Unserialize(parent->GetObject(srcIdx), parent->GetObjectSize(srcIdx));
         dstRep.Unserialize(parent->GetObject(dstIdx), parent->GetObjectSize(dstIdx));

         // The single distance this node pair costs.
         const double repToRep = Evaluator.GetDistance(srcRep, dstRep);
         St.Distances++;
         St.PairDistances++;

         stPage * srcPage = PageManager->GetPage(srcPageID);
         if (srcPage == NULL){
            return false;
         }//end if
         St.PageReads++;
         stSlimNode * srcNode = stSlimNode::CreateNode(srcPage);
         if (srcNode->GetNodeType() != stSlimNode::LEAF){
            St.PairsSkippedNotLeaf++;
            delete srcNode;
            PageManager->ReleasePage(srcPage);
            return false;
         }//end if

         stPage * dstPage = PageManager->GetPage(dstPageID);
         if (dstPage == NULL){
            delete srcNode;
            PageManager->ReleasePage(srcPage);
            return false;
         }//end if
         St.PageReads++;
         stSlimNode * dstNode = stSlimNode::CreateNode(dstPage);
         if (dstNode->GetNodeType() != stSlimNode::LEAF){
            St.PairsSkippedNotLeaf++;
            delete dstNode;
            PageManager->ReleasePage(dstPage);
            delete srcNode;
            PageManager->ReleasePage(srcPage);
            return false;
         }//end if

         stSlimLeafNode * srcLeaf = (stSlimLeafNode *) srcNode;
         stSlimLeafNode * dstLeaf = (stSlimLeafNode *) dstNode;

         // Taken from the leaf rather than from the parent entry: the leaf is
         // the authority, and reading it is free (a scan of stored distances).
         const double srcRadiusBefore = srcLeaf->GetMinimumRadius();

         // ---- Stage A: collect, without mutating anything -----------------
         std::vector<Candidate> accepted;
         const u_int32_t srcCount = srcLeaf->GetNumberOfEntries();

         for (u_int32_t t = 0; t < srcCount; t++){
            const double dxOi = srcLeaf->GetLeafEntry(t).Distance;

            // Equation 4.6. The representative is the entry at distance zero.
            if (dxOi == 0.0){
               St.RejectedByRepresentative++;
               continue;
            }//end if

            // Equation 4.3, lower bound - free, uses only stored values.
            if (std::fabs(dxOi - repToRep) > dstRadius){
               St.SkippedByBound++;
               continue;
            }//end if

            Candidate cand;
            cand.Index = t;
            cand.DistToSrcRep = dxOi;
            cand.Object.Unserialize(srcLeaf->GetObject(t),
                                    srcLeaf->GetObjectSize(t));

            // The cooldown is free, so it is tested before paying for a
            // distance. Off by default: the reverse relocation rate that H4
            // predicts has to be observable before it is damped.
            if (Cfg.CooldownQueries > 0){
               const std::string key = KeyOf(cand.Object);
               typename std::map<std::string, long>::iterator seen =
                     LastMoveQuery.find(key);
               if ((seen != LastMoveQuery.end()) &&
                   ((St.QueriesSeen - seen->second) < Cfg.CooldownQueries)){
                  St.RejectedByCooldown++;
                  continue;
               }//end if
            }//end if

            // Equation 4.3, exact - one distance, survivors only.
            cand.DistToDstRep = Evaluator.GetDistance(cand.Object, dstRep);
            St.Distances++;
            St.CandidateDistances++;

            if (cand.DistToDstRep > dstRadius){
               St.RejectedByCoverage++;
               continue;
            }//end if

            // A zero distance to the destination representative would create a
            // SECOND entry at distance 0, and GetRepresentativeEntry() returns
            // the first such entry - so the node would appear to have two
            // representatives.
            if (cand.DistToDstRep == 0.0){
               St.RejectedByDuplicate++;
               continue;
            }//end if

            accepted.push_back(cand);
         }//end for

         // ---- Stage B: farthest from O_i first (Equation 4.7) -------------
         std::sort(accepted.begin(), accepted.end(), FarthestFirst);

         // Largest d(x, O_i) among the entries that will NOT be moved. Since
         // `accepted` is sorted descending, after removing accepted[0..c-1] the
         // source radius is max(maxKept, accepted[c].DistToSrcRep). Removing
         // accepted[c] therefore shrinks the radius if and only if its distance
         // exceeds maxKept - which is what RequireShrink has to test, and what
         // a single stale `srcRadiusBefore` could not express.
         double maxKept = 0.0;
         {
            std::vector<bool> isAccepted(srcCount, false);
            for (std::size_t c = 0; c < accepted.size(); c++){
               isAccepted[accepted[c].Index] = true;
            }//end for
            for (u_int32_t t = 0; t < srcCount; t++){
               if (!isAccepted[t]){
                  const double d = srcLeaf->GetLeafEntry(t).Distance;
                  if (d > maxKept){
                     maxKept = d;
                  }//end if
               }//end if
            }//end for
         }

         std::vector<u_int32_t> toRemove;
         long removedSoFar = 0;

         for (std::size_t c = 0; (c < accepted.size()) && (movesLeft > 0); c++){
            Candidate & cand = accepted[c];

            if (((long) srcCount - removedSoFar - 1) < Cfg.MinLeafEntries){
               St.RejectedByUnderflow++;
               break;
            }//end if

            // Equation 4.8 gate: accept only a move that actually tightens the
            // source. Moving an object that is not the current farthest leaves
            // r_i untouched, so for k-NN it buys no extra pruning.
            if (Cfg.RequireShrink && (cand.DistToSrcRep <= maxKept)){
               St.RejectedByNoShrink++;
               continue;
            }//end if

            // Equation 4.5. AddEntry returns -1 when the page is full, so the
            // capacity test IS the insertion attempt - a failure costs nothing
            // and cannot lose the object. Objects are variable length here, so
            // a later, smaller one may still fit: continue rather than break.
            const int newIdx = dstLeaf->AddEntry(cand.Object.GetSerializedSize(),
                                                 cand.Object.Serialize());
            if (newIdx < 0){
               St.RejectedByCapacity++;
               continue;
            }//end if

            // AddEntry writes Offset and Occupation but NOT Distance. Leaving
            // it unset would corrupt every later triangle-inequality prefilter
            // in this node and silently lose query results.
            dstLeaf->GetLeafEntry(newIdx).Distance = cand.DistToDstRep;

            toRemove.push_back(cand.Index);
            removedSoFar++;
            movesLeft--;
            moved++;
            St.ObjectsRelocated++;

            const std::string key = KeyOf(cand.Object);
            typename std::map<std::string, u_int32_t>::iterator prev =
                  LastSource.find(key);
            if ((prev != LastSource.end()) && (prev->second == dstPageID)){
               St.ReverseRelocations++;
            }//end if
            LastSource[key] = srcPageID;
            LastMoveQuery[key] = St.QueriesSeen;
         }//end for

         bool changed = false;
         if (!toRemove.empty()){
            // Descending index order is the only order compaction cannot
            // invalidate: RemoveEntry shifts everything ABOVE the removed slot.
            std::sort(toRemove.begin(), toRemove.end());
            for (std::size_t r = toRemove.size(); r > 0; r--){
               srcLeaf->RemoveEntry(toRemove[r - 1]);
            }//end for

            // Equation 4.7 and the occupancy fix-up, both from stored distances
            // only - no metric calls. Same idiom the library's own SlimDown
            // uses when it rebuilds an index entry.
            const double srcRadiusAfter = srcLeaf->GetMinimumRadius();
            const double dstRadiusAfter = dstLeaf->GetMinimumRadius();

            parent->GetIndexEntry(srcIdx).Radius = srcRadiusAfter;
            parent->GetIndexEntry(srcIdx).NEntries = srcLeaf->GetNumberOfEntries();
            parent->GetIndexEntry(dstIdx).Radius = dstRadiusAfter;
            parent->GetIndexEntry(dstIdx).NEntries = dstLeaf->GetNumberOfEntries();

            // H2, checked rather than assumed. Equation 4.3 means every moved
            // object was already inside the destination ball, so its radius
            // cannot have grown.
            if (dstRadiusAfter > dstRadius){
               St.DestinationRadiusGrowthEvents++;
            }//end if

            St.RadiusShrinkTotal += (srcRadiusBefore - srcRadiusAfter);

            PageManager->WritePage(srcPage);
            PageManager->WritePage(dstPage);
            St.PageWrites += 2;
            changed = true;
         }//end if

         delete dstNode;
         PageManager->ReleasePage(dstPage);
         delete srcNode;
         PageManager->ReleasePage(srcPage);
         return changed;
      }//end RelocatePair

};//end stSlimEtapa1

#endif //ST_SLIM_TRACE

#endif //__STSLIMETAPA1_H
