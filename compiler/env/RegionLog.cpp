// TODO: Add header
#include "env/CompilerEnv.hpp"
#include "env/RegionLog.hpp"
#include <execinfo.h>
#include <libunwind.h>

RegionLog::RegionLog(int startTime, size_t startBytesAllocated, bool isStack) :
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
      _startTime = startTime;
      _startBytesAllocated = startBytesAllocated;
      _isStack = isStack;
      void *trace[REGION_BACKTRACE_DEPTH + 2];
      unw_backtrace(trace, REGION_BACKTRACE_DEPTH + 2);
      memcpy(_regionTrace, &trace[2], REGION_BACKTRACE_DEPTH * sizeof(void *));
      }

void
RegionLog::putOffset(TR::FILE *file, char *line)
   {
   if (char *targetExecFileStart = strstr(line, TARGET_EXECUTABLE_FILE))
      {
      // Check the assumption that the line is of the format <executable_file>(+0x<offset>)
      char *offset = strstr(targetExecFileStart, "(+0x");
      TR_ASSERT(offset != NULL, "Offset line not formatted as <executable_file>(+0x<offset>)\n");
      // We make such assumption to reduce the cost of checking each time for a faster output.
      char *offsetStart = offset + 4;   // Add 4 here is to skip prefix '(+0x'
      *strchr(offsetStart, ')') = '\0';
      TR::IO::fprintf(file, "%s ", offsetStart);
      }
   }

// double linked list modifyer functions
void 
RegionLog::regionLogListInsert(RegionLog **head, RegionLog **tail, RegionLog *target)
   {
   TR_ASSERT(target && head && tail, "argument NULL to call of regionLogListInsert\n");
   if (*tail == NULL)
      {
      *head = target;
      *tail = target;
      }
   else
      {
      TR_ASSERT(*head != NULL, "insert to tail but head is null\n");
      (*tail)->_next = target;
      target->_prev = *tail;
      *tail = target;
      }
   }

void 
RegionLog::regionLogListRemove(RegionLog **head, RegionLog **tail, RegionLog *target)
   {
   TR_ASSERT(target && head && tail, "argument NULL to call of regionLogListInsert\n");
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
      }
   else if (*tail == target)
      {
      // remove tail
      *tail = target->_prev;
      }
   if (target->_next)
      {
      target->_next->_prev = target->_prev;
      }
   if (target->_prev)
      {
      target->_prev->_next = target->_next;
      }
   target->_prev = NULL;
   target->_next = NULL;
   target->~RegionLog();
   }

void 
RegionLog::printRegionLog(TR::FILE *file)
   {
   TR::IO::fprintf(file, "%s %d %d %d %zu %zu %zu %zu %zu %zu %zu %zu\n", 
      this->_isStack ? "S" : "H", 0, this->_startTime, this->_endTime, 
      this->_bytesSegmentProviderAllocated, this->_bytesSegmentProviderFreed, 
      this->_bytesSegmentProviderInUseAllocated, this->_bytesSegmentProviderInUseFreed,
      this->_bytesSegmentProviderRealInUseAllocated, this->_bytesSegmentProviderRealInUseFreed,
      this->_startBytesAllocated, this->_endBytesAllocated);
   // next we print the stack traces of region
   char **regionConstrctorTraces = backtrace_symbols((void **)this->_regionTrace, REGION_BACKTRACE_DEPTH);
   for (int i = 0; i < REGION_BACKTRACE_DEPTH; i++)
      {
      putOffset(file, regionConstrctorTraces[i]);
      }
   free(regionConstrctorTraces);
   TR::IO::fprintf(file, "\n");
   // print stack traces with each callsite
   for (auto &allocPair : this->_allocMap)
      {
      TR::IO::fprintf(file, "%zu ", allocPair.second);
      char **allocationCallTraces = backtrace_symbols((void **)allocPair.first._trace, MAX_BACKTRACE_SIZE);
      for (int i = 0; i < MAX_BACKTRACE_SIZE; i++)
         {
         putOffset(file, allocationCallTraces[i]);
         }
      free(allocationCallTraces);
      TR::IO::fprintf(file, "\n");
      }
   }

// printRegionLogList of regionLogs after one head pointer. 
// In the end, all regions in a compilation will be collected in a double linked list of regionLogs and the head and tail maintained in segment provider.
// On destruction of a segment provider, dump results/insert the list to global list
void 
RegionLog::printRegionLogList(RegionLog *head, TR::FILE *file)
   {
   while (head)
      {
      head->printRegionLog(file);
      head = head->_next;
      }
   }

void 
RegionLog::accumulateMemoryIncrease(size_t deltaBytesAllocated, size_t deltaBytesInUse, size_t deltaRealInUse)
   {
   _bytesSegmentProviderAllocated += deltaBytesAllocated;
   _bytesSegmentProviderInUseAllocated += deltaBytesInUse;
   _bytesSegmentProviderRealInUseAllocated += deltaRealInUse;
   }

void 
RegionLog::accumulateMemoryRelease(size_t freedBytesAllocated, size_t freedBytesInUse, size_t freedRealBytesInUse)
   {
   _bytesSegmentProviderFreed += freedBytesAllocated;
   _bytesSegmentProviderInUseFreed += freedBytesInUse;
   _bytesSegmentProviderRealInUseFreed += freedRealBytesInUse;
   }

// size_t
// AllocEntry::Hash::operator()(const AllocEntry& k) const noexcept
//    {
//    size_t result = 0;
//    for (int i = 0; i < MAX_BACKTRACE_SIZE; i++) 
//       {
//       result ^= std::hash<void *>()(k._trace[i]);
//       }
//    return result;
//    }