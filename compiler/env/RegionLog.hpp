#ifndef OMR_REGION_LOG_HPP
#define OMR_REGION_LOG_HPP

#pragma once

#include <stddef.h>
#include <deque>
#include <stack>
#include "infra/ReferenceWrapper.hpp"
#include "env/TypedAllocator.hpp"
#include "env/MemorySegment.hpp"
#include "env/RawAllocator.hpp"
#include "env/PersistentAllocator.hpp"
#include <unordered_map>
#include <vector>

#define MAX_BACKTRACE_SIZE 10
#define REGION_BACKTRACE_DEPTH 3
#define TARGET_EXECUTABLE_FILE "libj9jit29.so"

template<typename K, typename V>
using PersistentUnorderedMapAllocator = TR::typed_allocator<std::pair<const K, V>, TR::PersistentAllocator &>;
template<typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
using PersistentUnorderedMap = std::unordered_map<K, V, H, E, PersistentUnorderedMapAllocator<K, V>>;

struct AllocEntry
   {
   void *_trace[MAX_BACKTRACE_SIZE];

   bool operator==(const AllocEntry &other) const 
      {
      return memcmp(_trace, other._trace, sizeof(void *)*MAX_BACKTRACE_SIZE) == 0;
      }
   };
namespace std {
   template <>
   struct hash<AllocEntry>
      {
      size_t operator()(const AllocEntry& k) const
         {
         size_t result = 0;
         for (int i = 0; i < MAX_BACKTRACE_SIZE; i++) 
            {
            result ^= hash<void *>()(k._trace[i]);
            }
         return result;
         }
      };
}

class RegionLog
   {
public:
   // Constructor for regionLog to keep the log of each region object
   RegionLog();
   ~RegionLog();
   static void regionLogListInsert(RegionLog **head, RegionLog **tail, RegionLog *target);
   static void regionLogListRemove(RegionLog **head, RegionLog **tail, RegionLog *target);
   static void printRegionLogList(RegionLog *head, FILE *file);

   bool _isHeap;
   void *_regionTrace[REGION_BACKTRACE_DEPTH];
   // Logical timestamp for start of the region
   int32_t _startTime;
   int32_t _endTime;
   size_t _bytesAllocated;
   size_t _startBytesAllocated;
   size_t _endBytesAllocated;
   size_t _bytesSegmentProviderAllocated;
   size_t _bytesSegmentProviderFreed;
   size_t _bytesSegmentProviderInUseAllocated;
   size_t _bytesSegmentProviderInUseFreed;
   size_t _bytesSegmentProviderRealInUseAllocated;
   size_t _bytesSegmentProviderRealInUseFreed;
   PersistentUnorderedMap<AllocEntry, size_t> _allocMap;
   // pointers to prev ane next
   RegionLog *_prev = NULL;
   RegionLog *_next = NULL;

   bool operator==(const RegionLog &other) const 
      {
      return memcmp(_regionTrace, other._regionTrace, sizeof(void *)*REGION_BACKTRACE_DEPTH) == 0;
      }

private:
   void printRegionLog(FILE *file);
   };

#endif