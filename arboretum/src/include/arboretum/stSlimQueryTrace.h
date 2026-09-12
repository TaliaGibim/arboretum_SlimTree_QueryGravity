/**
* @file
*
* Query traversal trace for the Slim-tree (thesis Chapter 4).
*
* WHY THIS EXISTS
* ---------------
* Chapter 4 needs two things the stock Slim-tree cannot provide:
*
*   - Section 4.5 asks for the number of nodes VISITED and PRUNED per query,
*     and, for k-NN, the value of tau and how many entries were actually
*     discarded after each tau update.
*   - Etapa 1 (section 4.3) needs to know, after a query has been answered,
*     which SIBLING nodes that query visited and how far their representatives
*     were from the query centre - that is the raw material for Equation 4.4.
*
* Both are pure observations of a traversal that already happens. Nothing here
* changes what a query returns.
*
* WHY IT IS A SEPARATE HEADER AND NOT PART OF stSlimTree
* -----------------------------------------------------
* stSlimTree offers only `public:` and `private:` - there is no `protected:` -
* and GetRoot() is private except under __stDEBUG__, which also switches on
* page clearing and range checks and so is unusable for measurement. A subclass
* therefore cannot instrument the traversal, and the recursive query overloads
* are private and non-virtual, so they cannot be overridden either.
*
* The compromise: the TRACE lives inside the tree (it must, the traversal is
* there), but everything that ACTS on the trace lives outside it. The relocation
* engine of Etapa 1 works purely through the page manager and the public node
* classes, driven by the page ids recorded here. The result is that the whole
* modification to upstream arboretum is a handful of one-line macro calls.
*
* ZERO COST WHEN DISABLED
* -----------------------
* ST_SLIM_TRACE defaults to 0. With it at 0 every macro below expands to
* ((void)0) and this header defines no types, no variables and pulls in no
* standard headers, so a preprocessed translation unit is identical to one built
* against pristine arboretum apart from these macro definitions. Build the
* Chapter 4 drivers with -DST_SLIM_TRACE=1.
*
* THREADING
* ---------
* stSlimActiveTrace is a single global pointer, so tracing is single-threaded by
* construction. The Chapter 4 drivers are single-threaded; do not enable tracing
* in a concurrent setting without making it thread_local first.
*/

#ifndef __STSLIMQUERYTRACE_H
#define __STSLIMQUERYTRACE_H

#ifndef ST_SLIM_TRACE
   /** Set to 1 to compile the traversal trace in. Off by default. */
   #define ST_SLIM_TRACE 0
#endif //ST_SLIM_TRACE

#if ST_SLIM_TRACE

#include <vector>
#include <cstddef>

//=============================================================================
// struct stSlimVisit
//-----------------------------------------------------------------------------
/**
* One parent-to-child edge that a query considered worth following.
*
* Recorded at the moment the child qualifies, which is also the moment the
* distance from the query centre to the child representative is known - so
* Equation 4.4 can later be decided with ZERO extra distance computations.
*/
struct stSlimVisit{
   /** Page id of the index node holding the entry. */
   u_int32_t Parent;
   /** Page id of the child node the entry points at. */
   u_int32_t Child;
   /**
   * Position of the entry inside the parent at the time of the visit.
   *
   * @warning Only a hint. A consumer must re-resolve the position by Child page
   * id, because the parent may have been rewritten since (Etapa 1 itself
   * rewrites parents).
   */
   u_int32_t EntryIdx;
   /** d(q, O_child) - the distance the traversal already paid for. */
   double DistToQuery;
   /** Covering radius of the child, as stored in the parent entry. */
   double Radius;
   /**
   * Whether the child node was actually READ.
   *
   * For a range query this is always true: the traversal is depth-first, so
   * qualifying and descending are the same event. For k-NN it is not - an entry
   * can be queued and then never expanded because tau shrank in the meantime.
   * Etapa 1 must only consider edges whose child was really visited, otherwise
   * it would reason about siblings the query never compared.
   */
   bool Expanded;
};//end stSlimVisit

//=============================================================================
// struct stSlimTrace
//-----------------------------------------------------------------------------
/**
* Everything one query traversal reveals about itself.
*
* All counters are per-query and are zeroed by Reset(), which the driver calls
* before each query.
*/
struct stSlimTrace{
   /** The parent-to-child edges the query considered. */
   std::vector<stSlimVisit> Visits;

   /** Nodes whose page was read (index + leaf). */
   long NodesEntered;
   /** Of those, how many were leaves. */
   long LeavesEntered;

   /** Entries looked at inside index nodes, including those pruned for free. */
   long IndexEntriesScanned;
   /** Of those, how many qualified and produced an edge. */
   long IndexEntriesEntered;
   /** Entries looked at inside leaf nodes. */
   long LeafEntriesScanned;

   /**
   * Distance computations spent on index entries.
   *
   * The metric evaluator keeps one global counter and cannot tell index-level
   * from leaf-level work. Counting the index ones here lets the driver derive
   *
   *     LeafDistances = (evaluator delta) - IndexDistances
   *
   * exactly, with no second hook in the leaf branches.
   */
   long IndexDistances;

   /** k-NN only: how many times the dynamic radius tau was tightened. */
   long TauUpdates;
   /** k-NN only: tau after the last update; the final search radius. */
   double FinalTau;
   /** k-NN only: the whole tau history, for the convergence plot of 4.8. */
   std::vector<double> TauTrace;
   /** k-NN only: entries taken off the priority queue. */
   long QueuePops;
   /**
   * k-NN only: pops rejected because tau had shrunk below their lower bound.
   *
   * This is literally the "entradas efetivamente descartadas apos a atualizacao
   * de tau" that section 4.5 asks to be recorded.
   */
   long QueuePopsDiscarded;

   stSlimTrace(){
      Reset();
   }//end stSlimTrace

   /**
   * Clears the trace so it can describe the next query.
   */
   void Reset(){
      Visits.clear();
      TauTrace.clear();
      NodesEntered = 0;
      LeavesEntered = 0;
      IndexEntriesScanned = 0;
      IndexEntriesEntered = 0;
      LeafEntriesScanned = 0;
      IndexDistances = 0;
      TauUpdates = 0;
      FinalTau = 0.0;
      QueuePops = 0;
      QueuePopsDiscarded = 0;
   }//end Reset

   /** Index nodes entered. */
   long GetIndexNodesEntered() const {
      return NodesEntered - LeavesEntered;
   }//end GetIndexNodesEntered

   /**
   * Subtrees rejected without reading their page.
   *
   * Derived rather than counted: an entry that was scanned but produced no edge
   * was pruned, either for free by the triangle inequality or by an explicit
   * distance evaluation.
   */
   long GetSubtreesPruned() const {
      return IndexEntriesScanned - IndexEntriesEntered;
   }//end GetSubtreesPruned

   /**
   * Marks the edge leading to the given page as actually expanded.
   *
   * Searches backwards because the edge that was queued most recently for a
   * page is the one being expanded now.
   */
   void MarkExpanded(u_int32_t pageID){
      for (std::size_t i = Visits.size(); i > 0; i--){
         if (Visits[i - 1].Child == pageID){
            Visits[i - 1].Expanded = true;
            return;
         }//end if
      }//end for
   }//end MarkExpanded
};//end stSlimTrace

/**
* The trace the current query writes into, or NULL when tracing is idle.
*
* An inline variable so the header stays self-contained across translation
* units. The tree only ever tests it for NULL and appends to it.
*/
inline stSlimTrace * stSlimActiveTrace = 0;

//=============================================================================
// Hooks
//-----------------------------------------------------------------------------

/** A node page was read. isLeaf distinguishes leaf from index. */
#define ST_SLIM_TRACE_ENTER(pageID, isLeaf)                                    \
   do{                                                                         \
      if (stSlimActiveTrace != 0){                                             \
         stSlimActiveTrace->NodesEntered++;                                    \
         if (isLeaf){ stSlimActiveTrace->LeavesEntered++; }                     \
      }                                                                        \
   }while(0)

/** n entries are about to be scanned in the node just entered. */
#define ST_SLIM_TRACE_SCAN(n, isLeaf)                                          \
   do{                                                                         \
      if (stSlimActiveTrace != 0){                                             \
         if (isLeaf){ stSlimActiveTrace->LeafEntriesScanned += (long)(n); }     \
         else { stSlimActiveTrace->IndexEntriesScanned += (long)(n); }          \
      }                                                                        \
   }while(0)

/** One distance was evaluated against an index entry representative. */
#define ST_SLIM_TRACE_IDIST()                                                  \
   do{                                                                         \
      if (stSlimActiveTrace != 0){ stSlimActiveTrace->IndexDistances++; }      \
   }while(0)

/**
* A child qualified and is being visited immediately (depth-first, range query).
*/
#define ST_SLIM_TRACE_DESCEND(parent, idx, child, radius, dq)                  \
   do{                                                                         \
      if (stSlimActiveTrace != 0){                                             \
         stSlimVisit v;                                                        \
         v.Parent = (parent);                                                  \
         v.Child = (child);                                                    \
         v.EntryIdx = (idx);                                                   \
         v.DistToQuery = (dq);                                                 \
         v.Radius = (radius);                                                  \
         v.Expanded = true;                                                    \
         stSlimActiveTrace->Visits.push_back(v);                               \
         stSlimActiveTrace->IndexEntriesEntered++;                             \
      }                                                                        \
   }while(0)

/**
* A child qualified and was put on the priority queue (best-first, k-NN).
*
* Expanded starts false; ST_SLIM_TRACE_EXPAND flips it if and when the queue
* actually gives the page back.
*/
#define ST_SLIM_TRACE_ENQUEUE(parent, idx, child, radius, dq)                  \
   do{                                                                         \
      if (stSlimActiveTrace != 0){                                             \
         stSlimVisit v;                                                        \
         v.Parent = (parent);                                                  \
         v.Child = (child);                                                    \
         v.EntryIdx = (idx);                                                   \
         v.DistToQuery = (dq);                                                 \
         v.Radius = (radius);                                                  \
         v.Expanded = false;                                                   \
         stSlimActiveTrace->Visits.push_back(v);                               \
         stSlimActiveTrace->IndexEntriesEntered++;                             \
      }                                                                        \
   }while(0)

/** The page taken off the queue is now being read (k-NN). */
#define ST_SLIM_TRACE_EXPAND(pageID)                                           \
   do{                                                                         \
      if (stSlimActiveTrace != 0){ stSlimActiveTrace->MarkExpanded(pageID); }  \
   }while(0)

/** The dynamic search radius tau was tightened (k-NN). */
#define ST_SLIM_TRACE_TAU(tau)                                                 \
   do{                                                                         \
      if (stSlimActiveTrace != 0){                                             \
         stSlimActiveTrace->TauUpdates++;                                      \
         stSlimActiveTrace->FinalTau = (tau);                                  \
         stSlimActiveTrace->TauTrace.push_back((double)(tau));                 \
      }                                                                        \
   }while(0)

/** An entry was popped from the queue; qualified says whether it survived tau. */
#define ST_SLIM_TRACE_POP(qualified)                                           \
   do{                                                                         \
      if (stSlimActiveTrace != 0){                                             \
         stSlimActiveTrace->QueuePops++;                                       \
         if (!(qualified)){ stSlimActiveTrace->QueuePopsDiscarded++; }          \
      }                                                                        \
   }while(0)

#else //ST_SLIM_TRACE

// Tracing compiled out: no types, no variables, no includes - only no-ops.
#define ST_SLIM_TRACE_ENTER(pageID, isLeaf)                   ((void)0)
#define ST_SLIM_TRACE_SCAN(n, isLeaf)                         ((void)0)
#define ST_SLIM_TRACE_IDIST()                                 ((void)0)
#define ST_SLIM_TRACE_DESCEND(parent, idx, child, radius, dq) ((void)0)
#define ST_SLIM_TRACE_ENQUEUE(parent, idx, child, radius, dq) ((void)0)
#define ST_SLIM_TRACE_EXPAND(pageID)                          ((void)0)
#define ST_SLIM_TRACE_TAU(tau)                                ((void)0)
#define ST_SLIM_TRACE_POP(qualified)                          ((void)0)

#endif //ST_SLIM_TRACE

#endif //__STSLIMQUERYTRACE_H
