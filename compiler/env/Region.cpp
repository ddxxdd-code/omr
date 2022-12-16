/*******************************************************************************
 * Copyright IBM Corp. and others 2000
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
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0-only WITH Classpath-exception-2.0 OR GPL-2.0-only WITH OpenJDK-assembly-exception-1.0
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
      regionLogListInsert(&(_segmentProvider._regionLogListHead), &(_segmentProvider._regionLogListTail), _regionLog);
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
      regionLogListInsert(&(_segmentProvider._regionLogListHead), &(_segmentProvider._regionLogListTail), _regionLog);
      }
   }

Region::~Region() throw()
   {
   if (_collectRegionLog)
      {
      size_t preReleaseBytesAllocated = _segmentProvider.bytesAllocated();
      size_t preReleaseBytesInUse = _segmentProvider.regionBytesInUse();
      size_t preReleaseBytesRealInUse = _segmentProvider.regionRealBytesInUse();
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
      // remove region log if total usage <= 4096
      if (_bytesAllocated <= 4096)
         {
         regionLogListRemove(&(_segmentProvider._regionLogListHead), &(_segmentProvider._regionLogListTail), _regionLog);
         return;
         }
         // log endtime
         _regionLog->_endTime = _segmentProvider.recordEvent();
         _regionLog->_endBytesAllocated = _segmentProvider.bytesAllocated();
         // log change in usage
         _regionAllocMap->_bytesSegmentProviderFreed += preReleaseBytesAllocated - _segmentProvider.bytesAllocated();
         _regionAllocMap->_bytesSegmentProviderInUseFreed += preReleaseBytesInUse - _segmentProvider.regionBytesInUse();
         _regionAllocMap->_bytesSegmentProviderRealInUseFreed += preReleaseBytesRealInUse - _segmentProvider.regionRealBytesInUse();
         // Get total bytes allocated
         _regionAllocMap->_bytesAllocated = bytesAllocated();
      }
   }

void *
Region::allocate(size_t const size, void *hint)
   {
   size_t const roundedSize = round(size);

   // log the allocate call stack traces
   if (_collectRegionLog)
      {
      struct AllocEntry entry;
      void *trace[MAX_BACKTRACE_SIZE + 1];
      unw_backtrace(trace, MAX_BACKTRACE_SIZE + 1);
      memcpy(entry._trace, &trace[1], MAX_BACKTRACE_SIZE * sizeof(void *));
      TR_ASSERT(_regionAllocMap, "regionAllocMap is not built");
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
      size_t preRequestBytesAllocated = _segmentProvider.bytesAllocated();
      size_t preRequestBytesInUse = _segmentProvider.regionBytesInUse();
      size_t preRequestBytesRealInUse = _segmentProvider.regionRealBytesInUse();
      }


   if (_currentSegment.get().remaining() >= roundedSize)
      {
      _bytesAllocated += roundedSize;

      // check case impossible
      if (_collectRegionLog && _segmentProvider.bytesAllocated() - preRequestBytesAllocated)
         {
         printf("segment provider changed for new memory allocation in segment, which should never happen!\n");
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
      _regionAllocMap->_bytesSegmentProviderAllocated += _segmentProvider.bytesAllocated() - preRequestBytesAllocated;
      _regionAllocMap->_bytesSegmentProviderInUseAllocated += _segmentProvider.regionBytesInUse() - preRequestBytesInUse;
      _regionAllocMap->_bytesSegmentProviderRealInUseAllocated += _segmentProvider.regionRealBytesInUse() - preRequestBytesRealInUse;
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
