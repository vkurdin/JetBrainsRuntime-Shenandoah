/*
 * Copyright (c) 2013, 2015, Red Hat, Inc. and/or its affiliates.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTMARK_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTMARK_HPP

#include "gc/shared/taskqueue.hpp"
#include "gc/shared/workgroup.hpp"
#include "gc/shenandoah/shenandoahPhaseTimings.hpp"
#include "gc/shenandoah/shenandoahOopClosures.hpp"
#include "gc/shenandoah/shenandoahTaskqueue.hpp"

class ShenandoahStrDedupQueue;

class ShenandoahConcurrentMark: public CHeapObj<mtGC> {
  friend class ShenandoahTraversalGC;
private:
  ShenandoahHeap* _heap;

  // The per-worker-thread work queues
  ShenandoahObjToScanQueueSet* _task_queues;

  ShenandoahSharedFlag _process_references;
  ShenandoahSharedFlag _unload_classes;

  ShenandoahSharedFlag _claimed_codecache;

  // Used for buffering per-region liveness data.
  // Needed since ShenandoahHeapRegion uses atomics to update liveness.
  //
  // The array has max-workers elements, each of which is an array of
  // jushort * max_regions. The choice of jushort is not accidental:
  // there is a tradeoff between static/dynamic footprint that translates
  // into cache pressure (which is already high during marking), and
  // too many atomic updates. size_t/jint is too large, jbyte is too small.
  jushort** _liveness_local;

private:
  template <class T, bool COUNT_LIVENESS>
  inline void do_task(ShenandoahObjToScanQueue* q, T* cl, jushort* live_data, ShenandoahMarkTask* task);

  template <class T>
  inline void do_chunked_array_start(ShenandoahObjToScanQueue* q, T* cl, oop array);

  template <class T>
  inline void do_chunked_array(ShenandoahObjToScanQueue* q, T* cl, oop array, int chunk, int pow);

  inline void count_liveness(jushort* live_data, oop obj);

  // Actual mark loop with closures set up
  template <class T, bool CANCELLABLE, bool DRAIN_SATB, bool COUNT_LIVENESS>
  void mark_loop_work(T* cl, jushort* live_data, uint worker_id, ParallelTaskTerminator *t);

  template <bool CANCELLABLE, bool DRAIN_SATB, bool COUNT_LIVENESS>
  void mark_loop_prework(uint worker_id, ParallelTaskTerminator *terminator, ReferenceProcessor *rp,
                         bool class_unload, bool update_refs, bool strdedup);

  // ------------------------ Currying dynamic arguments to template args ----------------------------

  template <bool CANCELLABLE, bool DRAIN_SATB>
  void mark_loop_2(uint w, ParallelTaskTerminator* t, ReferenceProcessor* rp,
                   bool count_liveness,
                   bool class_unload, bool update_refs, bool strdedup) {
    if (count_liveness) {
      mark_loop_prework<CANCELLABLE, DRAIN_SATB, true>(w, t, rp, class_unload, update_refs, strdedup);
    } else {
      mark_loop_prework<CANCELLABLE, DRAIN_SATB, false>(w, t, rp, class_unload, update_refs, strdedup);
    }
  };

  template <bool CANCELLABLE>
  void mark_loop_1(uint w, ParallelTaskTerminator* t, ReferenceProcessor* rp,
                   bool drain_satb, bool count_liveness,
                   bool class_unload, bool update_refs, bool strdedup) {
    if (drain_satb) {
      mark_loop_2<CANCELLABLE, true>(w, t, rp, count_liveness, class_unload, update_refs, strdedup);
    } else {
      mark_loop_2<CANCELLABLE, false>(w, t, rp, count_liveness, class_unload, update_refs, strdedup);
    }
  };

  // ------------------------ END: Currying dynamic arguments to template args ----------------------------

public:
  // Mark loop entry.
  // Translates dynamic arguments to template parameters with progressive currying.
  void mark_loop(uint worker_id, ParallelTaskTerminator* terminator, ReferenceProcessor *rp,
                 bool cancellable, bool drain_satb, bool count_liveness,
                 bool class_unload, bool update_refs, bool strdedup = false) {
    if (cancellable) {
      mark_loop_1<true>(worker_id, terminator, rp, drain_satb, count_liveness, class_unload, update_refs, strdedup);
    } else {
      mark_loop_1<false>(worker_id, terminator, rp, drain_satb, count_liveness, class_unload, update_refs, strdedup);
    }
  }

  // We need to do this later when the heap is already created.
  void initialize(uint workers);

  void set_process_references(bool pr);
  bool process_references() const;

  void set_unload_classes(bool uc);
  bool unload_classes() const;

  bool claim_codecache();
  void clear_claim_codecache();

  template<class T, UpdateRefsMode UPDATE_REFS>
  static inline void mark_through_ref(T* p, ShenandoahHeap* heap, ShenandoahObjToScanQueue* q);

  template<class T, UpdateRefsMode UPDATE_REFS, bool STRING_DEDUP>
  static inline void mark_through_ref(T* p, ShenandoahHeap* heap, ShenandoahObjToScanQueue* q, ShenandoahStrDedupQueue* dq = NULL);

  void mark_from_roots();

  // Prepares unmarked root objects by marking them and putting
  // them into the marking task queue.
  void init_mark_roots();
  void mark_roots(ShenandoahPhaseTimings::Phase root_phase);
  void update_roots(ShenandoahPhaseTimings::Phase root_phase);

  void shared_finish_mark_from_roots(bool full_gc);
  void finish_mark_from_roots();
  // Those are only needed public because they're called from closures.

  inline bool try_queue(ShenandoahObjToScanQueue* q, ShenandoahMarkTask &task);

  ShenandoahObjToScanQueue* get_queue(uint worker_id);
  void clear_queue(ShenandoahObjToScanQueue *q);

  inline bool try_draining_satb_buffer(ShenandoahObjToScanQueue *q, ShenandoahMarkTask &task);
  void drain_satb_buffers(uint worker_id, bool remark = false);
  ShenandoahObjToScanQueueSet* task_queues() { return _task_queues;}

  jushort* get_liveness(uint worker_id);

  void cancel();

  void preclean_weak_refs();

private:

  void weak_refs_work(bool full_gc);
  void weak_refs_work_doit(bool full_gc);

#if TASKQUEUE_STATS
  static void print_taskqueue_stats_hdr(outputStream* const st);
  void print_taskqueue_stats() const;
  void reset_taskqueue_stats();
#endif // TASKQUEUE_STATS

};

#endif // SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTMARK_HPP
