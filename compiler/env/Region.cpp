/*******************************************************************************
 * Copyright (c) 2000, 2022 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at http://eclipse.org/legal/epl-2.0
 * or the Apache License, Version 2.0 which accompanies this distribution
 * and is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following Secondary
 * Licenses when the conditions for such availability set forth in the
 * Eclipse Public License, v. 2.0 are satisfied: GNU General Public License,
 * version 2 with the GNU Classpath Exception [1] and GNU General Public
 * License, version 2 with the OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include <libunwind.h>

#include "env/MemorySegment.hpp"
#include "env/SegmentProvider.hpp"
#include "env/Region.hpp"
#include "infra/ReferenceWrapper.hpp"
#include "env/TRMemory.hpp"

namespace TR {

Region::Region(TR::SegmentProvider &segmentProvider, TR::RawAllocator rawAllocator, bool isHeap) :
   _bytesAllocated(0),
   _segmentProvider(segmentProvider),
   _rawAllocator(rawAllocator),
   _initialSegment(_initialSegmentArea.data, INITIAL_SEGMENT_SIZE),
   _currentSegment(TR::ref(_initialSegment)),
   _lastDestroyer(NULL),
   _collectRegionLog(false)
   {
   if (_segmentProvider.collectRegions())
      {
      _collectRegionLog = true;
      _regionLog = new (PERSISTENT_NEW) RegionLog;
      _regionLog->_isHeap = isHeap;
      // get timestamp
      _regionLog->_startTime = _segmentProvider.recordEvent();
      _regionLog->_startBytesAllocated = _segmentProvider.bytesAllocated();
      // log constructor into
      // collect backtrace of the constructor of the region
      void *trace[REGION_BACKTRACE_DEPTH + 1];
      unw_backtrace(trace, REGION_BACKTRACE_DEPTH + 1);
      memcpy(_regionLog->_regionTrace, &trace[1], REGION_BACKTRACE_DEPTH * sizeof(void *));
      // add regionLog to dllist in _segmentProvider
      RegionLog::regionLogListInsert(_segmentProvider.getRegionLogListHead(), _segmentProvider.getRegionLogListTail(), _regionLog);
      }
   }

Region::Region(const Region &prototype, bool isHeap) :
   _bytesAllocated(0),
   _segmentProvider(prototype._segmentProvider),
   _rawAllocator(prototype._rawAllocator),
   _initialSegment(_initialSegmentArea.data, INITIAL_SEGMENT_SIZE),
   _currentSegment(TR::ref(_initialSegment)),
   _lastDestroyer(NULL),
   _collectRegionLog(false)
   {
   if (_segmentProvider.collectRegions())
      {
      _collectRegionLog = true;
      _regionLog = new (PERSISTENT_NEW) RegionLog;
      _regionLog->_isHeap = isHeap;
      // get timestamp
      _regionLog->_startTime = _segmentProvider.recordEvent();
      _regionLog->_startBytesAllocated = _segmentProvider.bytesAllocated();
      // log constructor into
      // collect backtrace of the constructor of the region
      void *trace[REGION_BACKTRACE_DEPTH + 1];
      unw_backtrace(trace, REGION_BACKTRACE_DEPTH + 1);
      memcpy(_regionLog->_regionTrace, &trace[1], REGION_BACKTRACE_DEPTH * sizeof(void *));
      // add regionLog to dllist in segmentProvider
      RegionLog::regionLogListInsert(_segmentProvider.getRegionLogListHead(), _segmentProvider.getRegionLogListTail(), _regionLog);
      }
   }

Region::~Region() throw()
   {
   size_t preReleaseBytesAllocated = 0;
   size_t preReleaseBytesInUse = 0;
   size_t preReleaseBytesRealInUse = 0;
   if (_collectRegionLog)
      {
      preReleaseBytesAllocated = _segmentProvider.bytesAllocated();
      preReleaseBytesInUse = _segmentProvider.regionBytesInUse();
      preReleaseBytesRealInUse = _segmentProvider.regionRealBytesInUse();
      }
   /*
    * Destroy all object instances that depend on the region
    * to manage their lifetimes.
    */
   Destroyer *lastDestroyer = _lastDestroyer;
   while (lastDestroyer != NULL)
      {
      lastDestroyer->destroy();
      lastDestroyer = lastDestroyer->prev();
      }

   for (
      TR::reference_wrapper<TR::MemorySegment> latestSegment(_currentSegment);
      latestSegment.get() != _initialSegment;
      latestSegment = _currentSegment
      )
      {
      _currentSegment = TR::ref(latestSegment.get().unlink());
      _segmentProvider.release(latestSegment);
      }
   TR_ASSERT(_currentSegment.get() == _initialSegment, "self-referencial link was broken");

   
   // log changes only when we need them
   if (_collectRegionLog)
      {
      if (bytesAllocated() <= INITIAL_SEGMENT_SIZE)
         {
         // discard region of no memory used outside of initial segment
         RegionLog::regionLogListRemove(_segmentProvider.getRegionLogListHead(), _segmentProvider.getRegionLogListTail(), _regionLog);
         return;
         }
         // log endtime
         _regionLog->_endTime = _segmentProvider.recordEvent();
         _regionLog->_endBytesAllocated = _segmentProvider.bytesAllocated();
         // log change in usage
         _regionLog->_bytesSegmentProviderFreed += preReleaseBytesAllocated - _segmentProvider.bytesAllocated();
         _regionLog->_bytesSegmentProviderInUseFreed += preReleaseBytesInUse - _segmentProvider.regionBytesInUse();
         _regionLog->_bytesSegmentProviderRealInUseFreed += preReleaseBytesRealInUse - _segmentProvider.regionRealBytesInUse();
         // Get total bytes allocated
         _regionLog->_bytesAllocated = bytesAllocated();
      }
   }

void *
Region::allocate(size_t const size, void *hint)
   {
   size_t const roundedSize = round(size);

   // log the allocate call stack traces
   size_t preRequestBytesAllocated = 0;
   size_t preRequestBytesInUse = 0;
   size_t preRequestBytesRealInUse = 0;
   if (_collectRegionLog)
      {
      struct AllocEntry entry;
      void *trace[MAX_BACKTRACE_SIZE + 1];
      unw_backtrace(trace, MAX_BACKTRACE_SIZE + 1);
      memcpy(entry._trace, &trace[1], MAX_BACKTRACE_SIZE * sizeof(void *));
      TR_ASSERT(_regionLog, "regionLog is not built");
      auto match = _regionLog->_allocMap.find(entry);
      if (match != _regionLog->_allocMap.end())
         {
         match->second += roundedSize;
         }
      else
         {
         _regionLog->_allocMap.insert({entry, roundedSize});
         }
      
      // collect current usage
      preRequestBytesAllocated = _segmentProvider.bytesAllocated();
      preRequestBytesInUse = _segmentProvider.regionBytesInUse();
      preRequestBytesRealInUse = _segmentProvider.regionRealBytesInUse();
      }


   if (_currentSegment.get().remaining() >= roundedSize)
      {
      _bytesAllocated += roundedSize;

      // check case impossible
      if (_collectRegionLog && _segmentProvider.bytesAllocated() - preRequestBytesAllocated)
         {
         fprintf(stderr, "segment provider changed for new memory allocation in segment, which should never happen!\n");
         }

      return _currentSegment.get().allocate(roundedSize);
      }
   TR::MemorySegment &newSegment = _segmentProvider.request(roundedSize);
   TR_ASSERT(newSegment.remaining() >= roundedSize, "Allocated segment is too small");
   newSegment.link(_currentSegment.get());
   _currentSegment = TR::ref(newSegment);
   _bytesAllocated += roundedSize;

   // log change in usage if needed
   if (_collectRegionLog)
      {
      _regionLog->_bytesSegmentProviderAllocated += _segmentProvider.bytesAllocated() - preRequestBytesAllocated;
      _regionLog->_bytesSegmentProviderInUseAllocated += _segmentProvider.regionBytesInUse() - preRequestBytesInUse;
      _regionLog->_bytesSegmentProviderRealInUseAllocated += _segmentProvider.regionRealBytesInUse() - preRequestBytesRealInUse;
      }

   return _currentSegment.get().allocate(roundedSize);
   }

void
Region::deallocate(void * allocation, size_t) throw()
   {
   }

size_t
Region::round(size_t bytes)
   {
   return (bytes+15) & (~15);
   }
}
