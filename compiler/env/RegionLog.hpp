/*******************************************************************************
 * Copyright (c) 2023, 2023 IBM Corp. and others
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
#ifndef OMR_REGION_LOG_HPP
#define OMR_REGION_LOG_HPP

#include "env/IO.hpp"
#include "env/PersistentAllocator.hpp"
#include <unordered_map>

#define MAX_BACKTRACE_SIZE 10 // Need 10 levels of backtracing to get the call site of a Region::allocate() call to fully recover the call site outside of wrapped lib functions
#define REGION_BACKTRACE_DEPTH 1
#define TARGET_EXECUTABLE_FILE "libj9jit" // Locate compiled .so file to reduce common prefix in translated backtraces.

template<typename K, typename V>
using PersistentUnorderedMapAllocator = TR::typed_allocator<std::pair<const K, V>, TR::PersistentAllocator &>;
template<typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
using PersistentUnorderedMap = std::unordered_map<K, V, H, E, PersistentUnorderedMapAllocator<K, V>>;

struct AllocEntry
   {
   void *_trace[MAX_BACKTRACE_SIZE];
   struct Hash { size_t operator()(const AllocEntry& k) const noexcept; };

   bool operator==(const AllocEntry &other) const 
      {
      return memcmp(_trace, other._trace, sizeof(void *)*MAX_BACKTRACE_SIZE) == 0;
      }
   };

class RegionLog
   {
public:
   // Constructor for regionLog to keep the log of each region object
   RegionLog(int startTime, size_t startBytesAllocated, bool isStack);
   ~RegionLog() {};
   static void regionLogListInsert(RegionLog **head, RegionLog **tail, RegionLog *target);
   static void regionLogListRemove(RegionLog **head, RegionLog **tail, RegionLog *target);
   static void printRegionLogList(RegionLog *head, TR::FILE *file);
   void setStartTime(int startTime) { _startTime = startTime; }
   void setEndTime(int endTime) { _endTime = endTime; }
   void setBytesAllocated(size_t bytesAllocated) { _bytesAllocated = bytesAllocated; };
   void setStartBytesAllocated(size_t startBytesAllocated) { _startBytesAllocated = startBytesAllocated; };
   void setEndBytesAllocated(size_t endBytesAllocated) { _endBytesAllocated = endBytesAllocated; };
   void accumulateMemoryIncrease(size_t deltaBytesAllocated, size_t deltaBytesInUse, size_t deltaRealInUse);
   void accumulateMemoryRelease(size_t freedBytesAllocated, size_t freedBytesInUse, size_t freedRealBytesInUse);
   bool getIsStack() const { return _isStack; };
   int getStartTime() const { return _startTime; };
   int getEndTime() const { return _endTime; };
   size_t getBytesAllocated() const { return _bytesAllocated; };
   size_t getStartBytesAllocated() const { return _startBytesAllocated; };
   size_t getEndBytesAllocated() const { return _endBytesAllocated; };
   size_t getBytesSegmentProviderAllocated() const { return _bytesSegmentProviderAllocated; };
   size_t getBytesSegmentProviderFreed() const { return _bytesSegmentProviderFreed; };
   size_t getBytesSegmentProviderInUseAllocated() const { return _bytesSegmentProviderInUseAllocated; };
   size_t getBytesSegmentProviderInUseFreed() const { return _bytesSegmentProviderInUseFreed; };
   size_t getBytesSegmentProviderRealInUseAllocated() const { return _bytesSegmentProviderRealInUseAllocated; };
   size_t getBytesSegmentProviderRealInUseFreed() const { return _bytesSegmentProviderRealInUseFreed; };

   void *_regionTrace[REGION_BACKTRACE_DEPTH];
   // Logical timestamp for start of the region
   PersistentUnorderedMap<AllocEntry, size_t, AllocEntry::Hash> _allocMap;

   // pointers to prev ane next
   RegionLog *_prev = NULL;
   RegionLog *_next = NULL;

private:
   void putOffset(TR::FILE *file, char *line);
   void printRegionLog(TR::FILE *file);

   bool _isStack;
   int _startTime;
   int _endTime;
   size_t _bytesAllocated;
   size_t _startBytesAllocated;
   size_t _endBytesAllocated;
   size_t _bytesSegmentProviderAllocated;
   size_t _bytesSegmentProviderFreed;
   size_t _bytesSegmentProviderInUseAllocated;
   size_t _bytesSegmentProviderInUseFreed;
   size_t _bytesSegmentProviderRealInUseAllocated;
   size_t _bytesSegmentProviderRealInUseFreed;
   };

#endif