/*
 * Copyright (c) 2018, Red Hat, Inc. and/or its affiliates.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
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

#include "precompiled.hpp"
#include "gc/shared/gcArguments.inline.hpp"
#include "gc/shared/taskqueue.hpp"
#include "gc/shenandoah/shenandoahArguments.hpp"
#include "gc/shenandoah/shenandoahCollectorPolicy.hpp"
#include "gc/shenandoah/shenandoahHeap.hpp"
#include "utilities/defaultStream.hpp"

void ShenandoahArguments::initialize() {

#if !(defined AARCH64 || defined AMD64 || defined IA32)
  vm_exit_during_initialization("Shenandoah GC is not supported on this platform.");
#endif

#ifdef IA32
  log_warning(gc)("Shenandoah GC is not fully supported on this platform:");
  log_warning(gc)("  concurrent modes are not supported, only STW cycles are enabled;");
  log_warning(gc)("  arch-specific barrier code is not implemented, disabling barriers;");

  FLAG_SET_DEFAULT(ShenandoahGCHeuristics,           "passive");

  FLAG_SET_DEFAULT(ShenandoahSATBBarrier,            false);
  FLAG_SET_DEFAULT(ShenandoahKeepAliveBarrier,       false);
  FLAG_SET_DEFAULT(ShenandoahWriteBarrier,           false);
  FLAG_SET_DEFAULT(ShenandoahReadBarrier,            false);
  FLAG_SET_DEFAULT(ShenandoahStoreValEnqueueBarrier, false);
  FLAG_SET_DEFAULT(ShenandoahStoreValReadBarrier,    false);
  FLAG_SET_DEFAULT(ShenandoahCASBarrier,             false);
  FLAG_SET_DEFAULT(ShenandoahAcmpBarrier,            false);
  FLAG_SET_DEFAULT(ShenandoahCloneBarrier,           false);
  FLAG_SET_DEFAULT(UseShenandoahMatrix,              false);
#endif

#ifdef _LP64
  // The optimized ObjArrayChunkedTask takes some bits away from the full 64 addressable
  // bits, fail if we ever attempt to address more than we can. Only valid on 64bit.
  if (MaxHeapSize >= ObjArrayChunkedTask::max_addressable()) {
    jio_fprintf(defaultStream::error_stream(),
                "Shenandoah GC cannot address more than " SIZE_FORMAT " bytes, and " SIZE_FORMAT " bytes heap requested.",
                ObjArrayChunkedTask::max_addressable(), MaxHeapSize);
    vm_exit(1);
  }
#endif

  FLAG_SET_DEFAULT(ParallelGCThreads,
                   Abstract_VM_Version::parallel_worker_threads());

  if (FLAG_IS_DEFAULT(ConcGCThreads)) {
    uint conc_threads = MAX2((uint) 1, ParallelGCThreads);
    FLAG_SET_DEFAULT(ConcGCThreads, conc_threads);
  }

  if (FLAG_IS_DEFAULT(ParallelRefProcEnabled)) {
    FLAG_SET_DEFAULT(ParallelRefProcEnabled, true);
  }

  if (ShenandoahRegionSampling && FLAG_IS_DEFAULT(PerfDataMemorySize)) {
    // When sampling is enabled, max out the PerfData memory to get more
    // Shenandoah data in, including Matrix.
    FLAG_SET_DEFAULT(PerfDataMemorySize, 2048*K);
  }

#ifdef COMPILER2
  // Shenandoah cares more about pause times, rather than raw throughput.
  if (FLAG_IS_DEFAULT(UseCountedLoopSafepoints)) {
    FLAG_SET_DEFAULT(UseCountedLoopSafepoints, true);
    if (FLAG_IS_DEFAULT(LoopStripMiningIter)) {
      FLAG_SET_DEFAULT(LoopStripMiningIter, 1000);
    }
  }
#ifdef ASSERT
  // C2 barrier verification is only reliable when all default barriers are enabled
  if (ShenandoahVerifyOptoBarriers &&
          (!FLAG_IS_DEFAULT(ShenandoahSATBBarrier)            ||
           !FLAG_IS_DEFAULT(ShenandoahKeepAliveBarrier)       ||
           !FLAG_IS_DEFAULT(ShenandoahWriteBarrier)           ||
           !FLAG_IS_DEFAULT(ShenandoahReadBarrier)            ||
           !FLAG_IS_DEFAULT(ShenandoahStoreValEnqueueBarrier) ||
           !FLAG_IS_DEFAULT(ShenandoahStoreValReadBarrier)    ||
           !FLAG_IS_DEFAULT(ShenandoahCASBarrier)             ||
           !FLAG_IS_DEFAULT(ShenandoahAcmpBarrier)            ||
           !FLAG_IS_DEFAULT(ShenandoahCloneBarrier)
          )) {
    warning("Unusual barrier configuration, disabling C2 barrier verification");
    FLAG_SET_DEFAULT(ShenandoahVerifyOptoBarriers, false);
  }
#else
  guarantee(!ShenandoahVerifyOptoBarriers, "Should be disabled");
#endif // ASSERT
#endif // COMPILER2

  if (AlwaysPreTouch) {
    // Shenandoah handles pre-touch on its own. It does not let the
    // generic storage code to do the pre-touch before Shenandoah has
    // a chance to do it on its own.
    FLAG_SET_DEFAULT(AlwaysPreTouch, false);
    FLAG_SET_DEFAULT(ShenandoahAlwaysPreTouch, true);
  }

  // Shenandoah C2 optimizations apparently dislike the shape of thread-local handshakes.
  // Disable it by default, unless we enable it specifically for debugging.
  if (FLAG_IS_DEFAULT(ThreadLocalHandshakes)) {
    if (ThreadLocalHandshakes) {
      FLAG_SET_DEFAULT(ThreadLocalHandshakes, false);
    }
  } else {
    if (ThreadLocalHandshakes) {
      warning("Thread-local handshakes are not working correctly with Shenandoah at the moment. Enable at your own risk.");
    }
  }

  // Record more information about previous cycles for improved debugging pleasure
  if (FLAG_IS_DEFAULT(LogEventsBufferEntries)) {
    FLAG_SET_DEFAULT(LogEventsBufferEntries, 250);
  }

  if (ShenandoahConcurrentEvacCodeRoots) {
    if (!ShenandoahBarriersForConst) {
      if (FLAG_IS_DEFAULT(ShenandoahBarriersForConst)) {
        warning("Concurrent code cache evacuation is enabled, enabling barriers for constants.");
        FLAG_SET_DEFAULT(ShenandoahBarriersForConst, true);
      } else {
        warning("Concurrent code cache evacuation is enabled, but barriers for constants are disabled. "
                "This may lead to surprising crashes.");
      }
    }
  } else {
    if (ShenandoahBarriersForConst) {
      if (FLAG_IS_DEFAULT(ShenandoahBarriersForConst)) {
        warning("Concurrent code cache evacuation is disabled, disabling barriers for constants.");
        FLAG_SET_DEFAULT(ShenandoahBarriersForConst, false);
      }
    }
  }

  if (ShenandoahAlwaysPreTouch) {
    if (!FLAG_IS_DEFAULT(ShenandoahUncommit)) {
      warning("AlwaysPreTouch is enabled, disabling ShenandoahUncommit");
    }
    FLAG_SET_DEFAULT(ShenandoahUncommit, false);
  }
}

size_t ShenandoahArguments::conservative_max_heap_alignment() {
  return ShenandoahMaxRegionSize;
}

CollectedHeap* ShenandoahArguments::create_heap() {
  return create_heap_with_policy<ShenandoahHeap, ShenandoahCollectorPolicy>();
}
