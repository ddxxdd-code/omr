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

Region::Region(TR::SegmentProvider &segmentProvider, TR::RawAllocator rawAllocator, bool isStack) :
   _bytesAllocated(0),
   _segmentProvider(segmentProvider),
   _rawAllocator(rawAllocator),
   _initialSegment(_initialSegmentArea.data, INITIAL_SEGMENT_SIZE),
   _currentSegment(TR::ref(_initialSegment)),
   _lastDestroyer(NULL)
   {
   if (_segmentProvider.collectRegions())
      {
      _regionMemoryLog = new (PERSISTENT_NEW) RegionMemoryLog(_segmentProvider.recordEvent(), _segmentProvider.bytesAllocated(), isStack);
      // add regionMemoryLog to doublelinkedlist in _segmentProvider
      _segmentProvider.segmentProviderRegionMemoryLogListInsert(_regionMemoryLog);
      }
   }

Region::Region(const Region &prototype, bool isStack) :
   _bytesAllocated(0),
   _segmentProvider(prototype._segmentProvider),
   _rawAllocator(prototype._rawAllocator),
   _initialSegment(_initialSegmentArea.data, INITIAL_SEGMENT_SIZE),
   _currentSegment(TR::ref(_initialSegment)),
   _lastDestroyer(NULL)
   {
   if (_segmentProvider.collectRegions())
      {
      _regionMemoryLog = new (PERSISTENT_NEW) RegionMemoryLog(_segmentProvider.recordEvent(), _segmentProvider.bytesAllocated(), isStack);
      // add regionMemoryLog to doublelinkedlist in segmentProvider
      _segmentProvider.segmentProviderRegionMemoryLogListInsert(_regionMemoryLog);
      }
   }

Region::~Region() throw()
   {
   size_t preReleaseBytesAllocated = 0;
   size_t preReleaseBytesInUse = 0;
   size_t preReleaseBytesRealInUse = 0;
   if (_regionMemoryLog)
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
   if (_regionMemoryLog)
      {
      if (bytesAllocated() <= INITIAL_SEGMENT_SIZE)
         {
         // discard region of no memory used outside of initial segment
         _segmentProvider.segmentProviderRegionMemoryLogListRemove(_regionMemoryLog);
         return;
         }
         // log endtime
         _regionMemoryLog->setEndTime(_segmentProvider.recordEvent());
         _regionMemoryLog->setEndBytesAllocated(_segmentProvider.bytesAllocated());
         // log change in usage
         _regionMemoryLog->accumulateMemoryRelease(
            preReleaseBytesAllocated - _segmentProvider.bytesAllocated(), 
            preReleaseBytesInUse - _segmentProvider.regionBytesInUse(), 
            preReleaseBytesRealInUse - _segmentProvider.regionRealBytesInUse());
         // Get total bytes allocated
         _regionMemoryLog->setBytesAllocated(bytesAllocated());
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
   if (_regionMemoryLog)
      {
      struct AllocEntry entry;
      void *trace[MAX_BACKTRACE_SIZE + 1];
      unw_backtrace(trace, MAX_BACKTRACE_SIZE + 1);
      memcpy(entry._trace, &trace[1], MAX_BACKTRACE_SIZE * sizeof(void *));
      TR_ASSERT(_regionMemoryLog, "regionMemoryLog is not built");
      auto match = _regionMemoryLog->_allocMap.find(entry);
      if (match != _regionMemoryLog->_allocMap.end())
         {
         match->second += roundedSize;
         }
      else
         {
         _regionMemoryLog->_allocMap.insert({entry, roundedSize});
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
      TR_ASSERT(_regionMemoryLog && (_segmentProvider.bytesAllocated() - preRequestBytesAllocated > 0), "segment provider changed for new memory allocation in segment, which should never happen!\n");

      return _currentSegment.get().allocate(roundedSize);
      }
   TR::MemorySegment &newSegment = _segmentProvider.request(roundedSize);
   TR_ASSERT(newSegment.remaining() >= roundedSize, "Allocated segment is too small");
   newSegment.link(_currentSegment.get());
   _currentSegment = TR::ref(newSegment);
   _bytesAllocated += roundedSize;

   // log change in usage if needed
   if (_regionMemoryLog)
      {
      _regionMemoryLog->accumulateMemoryIncrease(_segmentProvider.bytesAllocated() - preRequestBytesAllocated,
         _segmentProvider.regionBytesInUse() - preRequestBytesInUse,
         _segmentProvider.regionRealBytesInUse() - preRequestBytesRealInUse);
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
