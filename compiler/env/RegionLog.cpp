#include <stddef.h>
#include "infra/ReferenceWrapper.hpp"
#include "env/TypedAllocator.hpp"
#include "env/MemorySegment.hpp"
#include "env/RawAllocator.hpp"
#include "env/PersistentAllocator.hpp"
#include "env/CompilerEnv.hpp"
#include "env/RegionLog.hpp"
#include <unordered_map>
#include <execinfo.h>

RegionLog::RegionLog() :
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

RegionLog::~RegionLog()
      {
      }

void
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

// double linked list modifyer functions
void 
RegionLog::regionLogListInsert(RegionLog **head, RegionLog **tail, RegionLog *target)
    {
    if (!target || !head || !tail)
        {
        return;
        }
    if (*tail == NULL)
        {
        *head = target;
        *tail = target;
        }
    else
        {
        if (*head == NULL)
            {
            fprintf(stderr, "insert to tail but head is null\n");
            }
        (*tail)->_next = target;
        target->_prev = *tail;
        *tail = target;
        }
    }

void 
RegionLog::regionLogListRemove(RegionLog **head, RegionLog **tail, RegionLog *target)
    {
    if (!target)
        {
        return;
        }
    if (!(head && tail))
        {
        return;
        }
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
    else
        {
        // remove middle
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
RegionLog::printRegionLog(FILE *file)
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
        0, this->_startTime, this->_endTime, 
        this->_bytesSegmentProviderAllocated, this->_bytesSegmentProviderFreed, 
        this->_bytesSegmentProviderInUseAllocated, this->_bytesSegmentProviderInUseFreed,
        this->_bytesSegmentProviderRealInUseAllocated, this->_bytesSegmentProviderRealInUseFreed,
        this->_startBytesAllocated, this->_endBytesAllocated);
    // next we print the stack traces of region
    char **temp = backtrace_symbols((void **)this->_regionTrace, REGION_BACKTRACE_DEPTH);
    for (int i = 0; i < REGION_BACKTRACE_DEPTH; i++)
        {
        putOffset(file, temp[i]);
        }
    free(temp);
    fprintf(file, "\n");
    // print stack traces with each callsite
    for (auto &allocPair : this->_allocMap)
        {
        fprintf(file, "%zu ", allocPair.second);
        temp = backtrace_symbols((void **)allocPair.first._trace, MAX_BACKTRACE_SIZE);
        for (int i = 0; i < MAX_BACKTRACE_SIZE; i++)
        {
        putOffset(file, temp[i]);
        }
        free(temp);
        fprintf(file, "\n");
        }
    }

// printRegionLogList of regionLogs after one head pointer. 
// In the end, all regions in a compilation will be collected in a double linked list of regionLogs and the head and tail maintained in segment provider.
// On destruction of a segment provider, dump results/insert the list to global list
void 
RegionLog::printRegionLogList(RegionLog *head, FILE *file)
    {
    while (head)
        {
        head->printRegionLog(file);
        head = head->_next;
        }
    }