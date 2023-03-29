// TODO: Add header
#ifndef OMR_REGION_LOG_HPP
#define OMR_REGION_LOG_HPP

#include "env/IO.hpp"
#include "env/PersistentAllocator.hpp"
#include <unordered_map>

#define MAX_BACKTRACE_SIZE 10
#define REGION_BACKTRACE_DEPTH 3
#define TARGET_EXECUTABLE_FILE "libj9jit"

template<typename K, typename V>
using PersistentUnorderedMapAllocator = TR::typed_allocator<std::pair<const K, V>, TR::PersistentAllocator &>;
template<typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
using PersistentUnorderedMap = std::unordered_map<K, V, H, E, PersistentUnorderedMapAllocator<K, V>>;

struct AllocEntry
   {
   void *_trace[MAX_BACKTRACE_SIZE];
   // struct Hash { size_t operator()(const AllocEntry& k) const noexcept; };

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
   bool getIsStack() { return _isStack; };
   int getStartTime() { return _startTime; };
   int getEndTime() { return _endTime; };
   size_t getBytesAllocated() { return _bytesAllocated; };
   size_t getStartBytesAllocated() { return _startBytesAllocated; };
   size_t getEndBytesAllocated() { return _endBytesAllocated; };
   size_t getBytesSegmentProviderAllocated() { return _bytesSegmentProviderAllocated; };
   size_t getBytesSegmentProviderFreed() { return _bytesSegmentProviderFreed; };
   size_t getBytesSegmentProviderInUseAllocated() { return _bytesSegmentProviderInUseAllocated; };
   size_t getBytesSegmentProviderInUseFreed() { return _bytesSegmentProviderInUseFreed; };
   size_t getBytesSegmentProviderRealInUseAllocated() { return _bytesSegmentProviderRealInUseAllocated; };
   size_t getBytesSegmentProviderRealInUseFreed() { return _bytesSegmentProviderRealInUseFreed; };

   void *_regionTrace[REGION_BACKTRACE_DEPTH];
   // Logical timestamp for start of the region
   PersistentUnorderedMap<AllocEntry, size_t> _allocMap;

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