#ifndef OMR_REGION_LOG_HPP
#define OMR_REGION_LOG_HPP

#pragma once

#include <deque>
#include <stddef.h>
#include <stdio.h>
#include <unordered_map>
#include "compile/Compilation.hpp"
#include "env/TypedAllocator.hpp"
#include "env/PersistentAllocator.hpp"

#define MAX_BACKTRACE_SIZE 10
#define REGION_BACKTRACE_DEPTH 3

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
   std::size_t operator()(const AllocEntry& k) const
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

static void
putOffset(std::FILE *file, char *line)
   {
   if (char *targetExecFileStart = strstr(line, TARGET_EXECUTABLE_FILE))
      {
      // Warning: here we make the assumption that the line is of the format <executable_file>(+0x<offset>)
      // without checking the format, doing below lines is risky
      // We make such assumption to reduce the cost of checking each time for a faster output.
      char *offsetStart = strchr(targetExecFileStart, '(') + 4;   // Add 4 here is to skip prefix '(+0x'
      *strchr(offsetStart, ')') = '\0';
      fprintf(file, "%s ", offsetStart);
      }
   }

class RegionLog
   {
private:
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
   // pointers to prev ane next
   regionLog *_prev = NULL;
   regionLog *_next = NULL;

   PersistentUnorderedMap<AllocEntry, size_t> _allocMap;

public:
   // Constructor for regionLog to keep the log of each region object
   RegionLog() :
   _startTime(-2),
   _endTime(-1),
   _bytesAllocated(0),
   _bytesSegmentProviderAllocated(0),
   _bytesSegmentProviderFreed(0),
   _bytesSegmentProviderInUseAllocated(0),
   _bytesSegmentProviderInUseFreed(0),
   _bytesSegmentProviderRealInUseAllocated(0),
   _bytesSegmentProviderRealInUseFreed(0),
   _startBytesAllocated(0),
   _endBytesAllocated(0),
   _prev(NULL),
   _next(NULL),
   _allocMap(PersistentUnorderedMap<AllocEntry, size_t>::allocator_type(TR::Compiler->persistentAllocator()))
      {
      }

   ~RegionLog()
      {
      }

   void printRegionLog(FILE *file)
      {
      if (this->_isHeap)
         {
         fprintf(file, "%s ", "H");
         }
      else
         {
         fprintf(file, "%s ", "S");
         }
      fprintf(file, "%d %d %d %zu %zu %zu %zu %zu %zu %zu %zu\n", 
         this->_sequenceNumber, this->_startTime, this->_endTime, 
         this->_bytesSegmentProviderAllocated, this->_bytesSegmentProviderFreed, 
         this->_bytesSegmentProviderInUseAllocated, this->_bytesSegmentProviderInUseFreed,
         this->_bytesSegmentProviderRealInUseAllocated, this->_bytesSegmentProviderRealInUseFreed,
         this->_startBytesAllocated, this->_endBytesAllocated);
      // next we print the stack traces of region
      char **temp = backtrace_symbols((void **)region->_regionTrace, REGION_BACKTRACE_DEPTH);
      for (int i = 0; i < REGION_BACKTRACE_DEPTH; i++)
         {
         putOffset(out_file, temp[i]);
         }
      free(temp);
      fprintf(out_file, "\n");
      // print stack traces with each callsite
      for (auto &allocPair : region->_allocMap)
         {
         fprintf(out_file, "%zu ", allocPair.second);
         temp = backtrace_symbols((void **)allocPair.first._trace, MAX_BACKTRACE_SIZE);
         for (int i = 0; i < MAX_BACKTRACE_SIZE; i++)
            {
            putOffset(out_file, temp[i]);
            }
         free(temp);
         fprintf(out_file, "\n");
         }
      }

   bool operator==(const RegionLog &other) const 
      {
      return memcmp(_regionTrace, other._regionTrace, sizeof(void *)*REGION_BACKTRACE_DEPTH) == 0;
      }
   };

// double linked list modifyer functions
void regionLogListInsert(regionLog **head, regionLog **tail, regionLog *target)
   {
   if (!*tail)
      {
      *head = target;
      *tail = target;
      return 0;
      }
   else
      {
      target->_prev = *tail;
      *tail = target;
      }
   }

void regionLogListRemove(regionLog **head, regionLog **tail, regionLog *target)
   {
   // cases: remove only, remove head, remove tail, remove middle
   if (*head == target && *tail == target)
      {
      // remove only
      *head = NULL;
      *tail = NULL;
      }
   else if (*head == target)
      {
      // remove head
      *head = target->_next;
      *head->_prev = NULL;
      }
   else if (*tail == target)
      {
      // remove tail
      *tail = target->_prev;
      *tail->_next = NULL;
      }
   else
      {
      // remove middle
      target->_prev->_next = target->_next;
      target->_next->_prev = target->_prev;
      }
   target->_prev = NULL;
   target->_next = NULL;
   }

#endif