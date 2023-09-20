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
#include "env/CompilerEnv.hpp"
#include "env/RegionMemoryLog.hpp"
#include <execinfo.h>
#include <libunwind.h>

RegionMemoryLog::RegionMemoryLog(int startTime, size_t startBytesAllocated, bool isStack) :
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
   _allocMap(decltype(_allocMap)::allocator_type(TR::Compiler->persistentAllocator()))
      {
      _startTime = startTime;
      _startBytesAllocated = startBytesAllocated;
      _isStack = isStack;
      if (isStack)
         {
         // Stack region has a wrapper outside of region's initializer, so need an extra level of backtrace to get the region's instanciation call
         void *trace[REGION_BACKTRACE_DEPTH + 3];
         unw_backtrace(trace, REGION_BACKTRACE_DEPTH + 3);
         memcpy(_regionTrace, &trace[2], REGION_BACKTRACE_DEPTH * sizeof(void *));
         }
      else
         {
         void *trace[REGION_BACKTRACE_DEPTH + 2];
         unw_backtrace(trace, REGION_BACKTRACE_DEPTH + 2);
         memcpy(_regionTrace, &trace[1], REGION_BACKTRACE_DEPTH * sizeof(void *));
         }
      }

void
RegionMemoryLog::putOffset(TR::FILE *file, char *line)
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
RegionMemoryLog::regionMemoryLogListInsert(RegionMemoryLog **head, RegionMemoryLog **tail, RegionMemoryLog *target)
   {
   TR_ASSERT(target && head && tail, "argument NULL to call of regionMemoryLogListInsert\n");
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
RegionMemoryLog::regionMemoryLogListRemove(RegionMemoryLog **head, RegionMemoryLog **tail, RegionMemoryLog *target)
   {
   TR_ASSERT(target && head && tail, "argument NULL to call of regionMemoryLogListInsert\n");
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
   target->~RegionMemoryLog();
   }

void 
RegionMemoryLog::printRegionMemoryLog(TR::FILE *file)
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

// printRegionMemoryLogList of regionMemoryLogs after one head pointer. 
// In the end, all regions in a compilation will be collected in a double linked list of regionMemoryLogs and the head and tail maintained in segment provider.
// On destruction of a segment provider, dump results/insert the list to global list
void 
RegionMemoryLog::printRegionMemoryLogList(RegionMemoryLog *head, TR::FILE *file)
   {
   while (head)
      {
      head->printRegionMemoryLog(file);
      head = head->_next;
      }
   }

void 
RegionMemoryLog::accumulateMemoryIncrease(size_t deltaBytesAllocated, size_t deltaBytesInUse, size_t deltaRealInUse)
   {
   _bytesSegmentProviderAllocated += deltaBytesAllocated;
   _bytesSegmentProviderInUseAllocated += deltaBytesInUse;
   _bytesSegmentProviderRealInUseAllocated += deltaRealInUse;
   }

void 
RegionMemoryLog::accumulateMemoryRelease(size_t freedBytesAllocated, size_t freedBytesInUse, size_t freedRealBytesInUse)
   {
   _bytesSegmentProviderFreed += freedBytesAllocated;
   _bytesSegmentProviderInUseFreed += freedBytesInUse;
   _bytesSegmentProviderRealInUseFreed += freedRealBytesInUse;
   }

size_t
AllocEntry::Hash::operator()(const AllocEntry& k) const noexcept
   {
   size_t result = 0;
   for (int i = 0; i < MAX_BACKTRACE_SIZE; i++) 
      {
      result ^= std::hash<void *>()(k._trace[i]);
      }
   return result;
   }
