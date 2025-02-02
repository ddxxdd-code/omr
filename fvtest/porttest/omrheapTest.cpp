/*******************************************************************************
 * Copyright IBM Corp. and others 1991
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

/*
 * $RCSfile: omrheapTest.c,v $
 * $Revision: 1.27 $
 * $Date: 2011-12-30 14:15:00 $
 */

/**
 * @file
 * @ingroup PortTest
 * @brief Verify port library heap management.
 *
 * Exercise the API for port library memory management operations.  These functions
 * can be found in the file @ref omrmem.c
 *
 * @note port library memory management operations are not optional in the port library table.
 *
 */
#include <stdlib.h>
#include <string.h>

#include "testHelpers.hpp"
#include "omrport.h"
#include "omrpool.h"
#include "pool_api.h"
#include "omrutil.h"

extern PortTestEnvironment *portTestEnv;

/**
 * The amount of metadata used for heap management in bytes.
 * We operate on the internal knowledge of sizeof(J9Heap) = 2*sizeof(uintptr_t), i.e. white box testing.
 * This information is required for us to walk the heap and check its integrity after an allocation or free.
 */
struct J9Heap {
	uintptr_t heapSize; /* total size of the heap in number of slots */
	uintptr_t firstFreeBlock; /* slot number of the first free block within the heap */
	uintptr_t lastAllocSlot; /* slot number for the last allocation */
	uintptr_t largestAllocSizeVisited; /* largest free list entry visited while performing the last allocation */
};

#define NON_J9HEAP_HEAP_OVERHEAD 2
#define SIZE_OF_J9HEAP_HEADER (sizeof(J9Heap))
#define HEAP_MANAGEMENT_OVERHEAD (SIZE_OF_J9HEAP_HEADER + NON_J9HEAP_HEAP_OVERHEAD*sizeof(uint64_t))
#define MINIMUM_HEAP_SIZE (HEAP_MANAGEMENT_OVERHEAD + sizeof(uint64_t))

typedef struct AllocListElement {
	uintptr_t allocSize;
	void *allocPtr;
	uintptr_t allocSerialNumber;
} AllocListElement;

static const int32_t outputInterval = 10;

static void walkHeap(struct OMRPortLibrary *portLibrary, J9Heap *heapBase, const char *testName);
static void verifySubAllocMem(struct OMRPortLibrary *portLibrary, void *subAllocMem, uintptr_t allocSize, J9Heap *heapBase, const char *testName);
static void iteratePool(struct OMRPortLibrary *portLibrary, J9Pool *allocPool);
static void *removeItemFromPool(struct OMRPortLibrary *portLibrary, J9Pool *allocPool, uintptr_t removeIndex);
static AllocListElement *getElementFromPool(struct OMRPortLibrary *portLibrary, J9Pool *allocPool, uintptr_t index);
static uintptr_t allocLargestChunkPossible(struct OMRPortLibrary *portLibrary, J9Heap *heapBase, uintptr_t heapSize);
static void freeRemainingElementsInPool(struct OMRPortLibrary *portLibrary, J9Heap *heapBase, J9Pool *allocPool);
static void verifyHeapOutofRegionWrite(struct OMRPortLibrary *portLibrary, uint8_t *memAllocStart, uint8_t *heapEnd, uintptr_t heapStartOffset, const char *testName);

/**
 * Verify port library heap sub-allocator.
 *
 * Ensure the port library is properly setup to run heap operations.
 */
TEST(PortHeapTest, heap_test0)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portTestEnv->getPortLibrary());
	const char *testName = "omrheap_test0";
	reportTestEntry(OMRPORTLIB, testName);

	if (NULL == OMRPORTLIB->heap_create) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "portLibrary->heap_create is NULL\n");
	}

	if (NULL == OMRPORTLIB->heap_allocate) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "portLibrary->heap_allocate is NULL\n");
	}

	if (NULL == OMRPORTLIB->heap_free) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "portLibrary->heap_free is NULL\n");
	}

	if (NULL == OMRPORTLIB->heap_reallocate) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "portLibrary->heap_reallocate is NULL\n");
	}

	if (NULL == OMRPORTLIB->heap_query_size) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "portLibrary->heap_query_size is NULL\n");
	}

	reportTestExit(OMRPORTLIB, testName);
}

/**
 * Verify port library heap sub-allocator.
 *
 * Ensure basic function of heap API. This test allocates 2MB of memory, and fill the memory with 1's.
 * It then enters a loop that calls omrheap_create on each of the first 16 byte-aligned locations with a heap size of 1MB.
 * After each omrheap_create, we perform a series of heap_alloc and heap_free from size 0 to the maximum possible.
 * And finally, we check the bytes after the heap memory to see if we have overwrote any of them.
 */
TEST(PortHeapTest, heap_test1)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portTestEnv->getPortLibrary());
	const char *testName = "omrheap_test1";

	/*malloc 1MB as backing storage for the heap*/
	uintptr_t memAllocAmount = 1 * 1024 * 1024;
	uint8_t *allocPtr = NULL;
	uintptr_t heapStartOffset = 50;
	uintptr_t heapSize[] = {10, 100, 1000, 512 * 1024};
	uintptr_t j;

	reportTestEntry(OMRPORTLIB, testName);
	portTestEnv->changeIndent(1);

	/* Allocate backing storage using mem_allocate and pass in the returned pointer and the allocated size
	 * Verify heap integrity after each heap operation
	 */
	allocPtr = (uint8_t *)omrmem_allocate_memory(memAllocAmount, OMRMEM_CATEGORY_PORT_LIBRARY);
	if (NULL == allocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to allocate %zu bytes for the heap\n", memAllocAmount);
		goto exit;
	} else {
		portTestEnv->log("allocPtr: 0x%p\n", allocPtr);
	}

	for (j = 0; j < (sizeof(heapSize) / sizeof(heapSize[0])); j++) {
		portTestEnv->log("test for heap size %zu bytes\n", heapSize[j]);
		/*we leave the first <heapStartOffset> bytes alone so that we can check if they are corrupted by heap operations*/
		uint8_t *allocPtrAdjusted = allocPtr + heapStartOffset;
		uintptr_t subAllocSize = 0;
		void *subAllocPtr = NULL;
		J9Heap *heapBase;

		memset(allocPtr, 0xff, memAllocAmount);
		heapBase = omrheap_create(allocPtrAdjusted, heapSize[j], 0);
		if (((NULL == heapBase) && heapSize[j] > MINIMUM_HEAP_SIZE) || ((NULL != heapBase) && heapSize[j] < MINIMUM_HEAP_SIZE)) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to create heap at %p with size %zu\n", allocPtrAdjusted, heapSize[j]);
			goto exit;
		}
		if (NULL == heapBase) {
			break;
		}
		do {
			subAllocPtr = omrheap_allocate(heapBase, subAllocSize);
			if (subAllocPtr) {
				uintptr_t querySize = omrheap_query_size(heapBase, subAllocPtr);
				/*The size returned may have been rounded-up to the nearest U64, and may have an extra 2 slots added in some circumstances*/
				if (querySize < subAllocSize || querySize > (subAllocSize + 3 * sizeof(uint64_t))) {
					outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_query_size returned the wrong size. Expected %zu, got %zu\n", subAllocSize, querySize);
					omrheap_free(heapBase, subAllocPtr);
					break;
				}
			}
			walkHeap(OMRPORTLIB, heapBase, testName);
			omrheap_free(heapBase, subAllocPtr);
			walkHeap(OMRPORTLIB, heapBase, testName);
			subAllocSize  += 4;
		} while (NULL != subAllocPtr);

		verifyHeapOutofRegionWrite(OMRPORTLIB, allocPtr, allocPtrAdjusted + heapSize[j], heapStartOffset, testName);
	}
	omrmem_free_memory(allocPtr);
	portTestEnv->changeIndent(-1);

exit:
	reportTestExit(OMRPORTLIB, testName);
}

/**
 * Verify port library heap sub-allocator.
 *
 * This is a more exhaustive test that performs random allocations and frees in a fairly large heap.
 * We go into a loop of heap_allocate and heap_free, the size of alloc is based on the random value (i.e. [0, allocSizeBoundary]).
 * We store the returned address, request size, etc in a sub-allocated memory identifier, which is stored in a J9Pool.
 * When we do a heap_free, we randomly pick an element from the pool and free the memory based on the values stored in the identifier.
 * We keep a separate counter for number of heap_allocate and heap_free calls, and we terminate the loop when heap_free count reaches a certain value e.g. 500,000.
 *
 * As an additional test for memory leak. We determine the largest amount of memory we can allocate before we enter the loop of heap_alloc/heap_free.
 * After the loop is terminated, we free any remaining memory chunks in the pool and again allocate the largest possible size.
 * If this allocation is no longer successful, it means we have leaked memory on the heap during our loop execution.
 *
 * @param[in] portLibrary The port library under test
 *
 * @return TEST_PASS on success, TEST_FAIL on error
 */
int
omrheap_test2(struct OMRPortLibrary *portLibrary, int randomSeed)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portLibrary);
	const char *testName = "omrheap_test2";
	uintptr_t allocSerialNumber = 0;
	uintptr_t freeSerialNumber = 0;
	uintptr_t reallocSerialNumber = 0;
	uintptr_t heapSize = 10 * 1024 * 1024;
	uintptr_t serialNumberTop = 10000;
	uintptr_t allocSizeBoundary = heapSize / 2;

	uintptr_t largestAllocSize = 0;
	J9Heap *allocHeapPtr = NULL;
	J9Heap *heapBase = NULL;
	void *subAllocMem;
	J9Pool *allocPool;

	reportTestEntry(OMRPORTLIB, testName);

	allocPool = pool_new(sizeof(AllocListElement),  0, 0, 0, OMR_GET_CALLSITE(), OMRMEM_CATEGORY_PORT_LIBRARY, POOL_FOR_PORT(OMRPORTLIB));
	if (NULL == allocPool) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to allocate pool\n", heapSize);
		goto exit;
	}

	allocHeapPtr = (J9Heap *)omrmem_allocate_memory(heapSize, OMRMEM_CATEGORY_PORT_LIBRARY);
	if (NULL == allocHeapPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to allocate %zu bytes for the heap\n", heapSize);
		goto exit;
	}
	heapBase = omrheap_create(allocHeapPtr, heapSize, 0);
	if (NULL == heapBase) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "failed to create the heap\n");
		goto exit;
	}
	walkHeap(OMRPORTLIB, heapBase, testName);

	/*attempt to allocate the largest possible chunk from the heap*/
	largestAllocSize = allocLargestChunkPossible(OMRPORTLIB, heapBase, heapSize);
	if (0 == largestAllocSize) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "failed to allocate the largest chunk\n");
		goto exit;
	}
	portTestEnv->log("Largest possible chunk size: %zu bytes\n", largestAllocSize);
	walkHeap(OMRPORTLIB, heapBase, testName);

	if (0 == randomSeed) {
		randomSeed = (int)omrtime_current_time_millis();
	}
	portTestEnv->log("Random seed value: %d. Add -srand:[seed] to the command line to reproduce this test manually.\n", randomSeed);
	srand(randomSeed);

	while (freeSerialNumber <= serialNumberTop) {
		uintptr_t allocSize = 0;
		BOOLEAN isHeapFull = FALSE;
		AllocListElement *allocElement;
		uintptr_t operation = (uintptr_t)rand() % 3;

		if (0 == operation) {
			allocSize = rand() % allocSizeBoundary;
			while ((subAllocMem = omrheap_allocate(heapBase, allocSize)) == NULL) {
				/*portTestEnv->log(LEVEL_ERROR, "**Failed omrheap_allocate, sizeRequested=%d\n", allocSize);*/
				allocSize = allocSize * 3 / 4;
				if (0 == allocSize) {
					/* No space left on the heap, go to next loop omrmem_free_memory(allocHeapPtr);
					 *  and hope we do a omrheap_free soon. */
					isHeapFull = TRUE;
					break;
				}
			}
			if (isHeapFull) {
				continue;
			}

			allocSerialNumber += 1;

			allocElement = (AllocListElement *)pool_newElement(allocPool);
			allocElement->allocPtr = subAllocMem;
			allocElement->allocSize = allocSize;
			allocElement->allocSerialNumber = allocSerialNumber;

			if (0 == allocSerialNumber % outputInterval) {
				portTestEnv->log(LEVEL_VERBOSE, "Alloc: size=%zu, allocSerialNumber=%zu, elementCount=%zu\n", allocSize, allocSerialNumber, pool_numElements(allocPool));
			}
			verifySubAllocMem(OMRPORTLIB, subAllocMem, allocSize, heapBase, testName);
			walkHeap(OMRPORTLIB, heapBase, testName);
			iteratePool(OMRPORTLIB, allocPool);
		} else if (1 == operation) {
			uintptr_t reallocIndex;
			if (0 == pool_numElements(allocPool)) {
				continue;
			}
			reallocIndex = rand() % pool_numElements(allocPool);
			allocElement = getElementFromPool(OMRPORTLIB, allocPool, reallocIndex);

			if (rand() & 1) {
				allocSize = allocElement->allocSize - 8 + (rand() % 16);
			} else {
				allocSize = rand() % allocSizeBoundary;
			}
			subAllocMem = omrheap_reallocate(heapBase, allocElement->allocPtr, allocSize);
			reallocSerialNumber += 1;

			if (NULL != subAllocMem) {
				allocElement->allocPtr = subAllocMem;
				allocElement->allocSize = allocSize;
			}

			if (0 == reallocSerialNumber % outputInterval) {
				portTestEnv->log(LEVEL_VERBOSE, "Realloc: index=%zu, size=%zu, result=%p, reallocSerialNumber=%zu, elementCount=%zu\n",
							  reallocIndex, allocSize, subAllocMem, freeSerialNumber, pool_numElements(allocPool));
			}
			walkHeap(OMRPORTLIB, heapBase, testName);
		} else {
			uintptr_t removeIndex;
			void *subAllocPtr;
			if (0 == pool_numElements(allocPool)) {
				continue;
			}
			removeIndex = rand() % pool_numElements(allocPool);

			subAllocPtr = removeItemFromPool(OMRPORTLIB, allocPool, removeIndex);

			freeSerialNumber += 1;

			omrheap_free(heapBase, subAllocPtr);
			if (0 == freeSerialNumber % outputInterval) {
				portTestEnv->log(LEVEL_VERBOSE, "Freed: index=%zu, freeSerialNumber=%zu, elementCount=%zu\n", removeIndex, freeSerialNumber, pool_numElements(allocPool));
			}
			walkHeap(OMRPORTLIB, heapBase, testName);
			iteratePool(OMRPORTLIB, allocPool);
		}
	}

	freeRemainingElementsInPool(OMRPORTLIB, heapBase, allocPool);
	walkHeap(OMRPORTLIB, heapBase, testName);
	if (NULL == omrheap_allocate(heapBase, largestAllocSize)) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Possible memory leak on the heap: "
						   "we cannot allocate the previously determined largest chunk size after sequence of random allocate/free\n");
		goto exit;
	}
	walkHeap(OMRPORTLIB, heapBase, testName);

exit:
	omrmem_free_memory(allocHeapPtr);
	pool_kill(allocPool);
	return reportTestExit(OMRPORTLIB, testName);
}

/**
 * Verify port library heap sub-allocator.
 *
 * Tests for integer overflow handling.
 *
 * Ensure the port library is properly setup to run heap operations.
 */
TEST(PortHeapTest, heap_test3)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portTestEnv->getPortLibrary());
	const char *testName = "omrheap_test3";

	/*malloc 1KB as backing storage for the heap*/
	uintptr_t memAllocAmount = 1024;
	uint8_t *allocPtr, *subAllocPtr;
	uintptr_t i;

	J9Heap *heapBase;

	reportTestEntry(OMRPORTLIB, testName);

	/* Allocate backing storage using mem_allocate and pass in the returned pointer and the allocated size
	 * Verify heap integrity after each heap operation
	 */
	allocPtr = (uint8_t *)omrmem_allocate_memory(memAllocAmount, OMRMEM_CATEGORY_PORT_LIBRARY);
	if (NULL == allocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to allocate %zu bytes for the heap\n", memAllocAmount);
		goto exit;
	}

	heapBase = omrheap_create(allocPtr, memAllocAmount, 0);
	if (NULL == allocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_create() failed!\n");
		goto exit;
	}

	for (i = 0; i < sizeof(uint64_t); i++) {
		subAllocPtr = (uint8_t *)omrheap_allocate(heapBase, UINT_MAX - i);
		if (NULL != subAllocPtr) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_allocate(%u) returned non-NULL!\n", UINT_MAX - i);
			goto exit;
		}
	}
	walkHeap(OMRPORTLIB, heapBase, testName);

	subAllocPtr = (uint8_t *)omrheap_allocate(heapBase, 128);
	for (i = 0; i < sizeof(uint64_t); i++) {
		subAllocPtr = (uint8_t *)omrheap_reallocate(heapBase, subAllocPtr, UINT_MAX - i);
		if (NULL != subAllocPtr) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_reallocate(%u) returned non-NULL!\n", UINT_MAX - i);
			goto exit;
		}
	}
	walkHeap(OMRPORTLIB, heapBase, testName);

	omrmem_free_memory(allocPtr);

exit:
	reportTestExit(OMRPORTLIB, testName);
}

/**
 * Verify port library heap sub-allocator.
 *
 * Tests that omrheap_reallocate() works as expected.
 *
 * Ensure the port library is properly setup to run heap operations.
 */
TEST(PortHeapTest, heap_realloc_test0)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portTestEnv->getPortLibrary());
	const char *testName = "omrheap_realloc_test0";

	/*malloc 2MB as backing storage for the heap*/
	uintptr_t memAllocAmount = 1024;
	uint8_t *allocPtr, *subAllocPtr, *subAllocPtrSaved, *subAllocPtr2;
	intptr_t i;

	J9Heap *heapBase;

	reportTestEntry(OMRPORTLIB, testName);

	/* Allocate backing storage using mem_allocate and pass in the returned pointer and the allocated size
	 * Verify heap integrity after each heap operation
	 */
	allocPtr = (uint8_t *)omrmem_allocate_memory(memAllocAmount, OMRMEM_CATEGORY_PORT_LIBRARY);
	if (NULL == allocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to allocate %zu bytes for the heap\n", memAllocAmount);
		goto exit;
	}

	heapBase = omrheap_create(allocPtr, memAllocAmount, 0);
	if (NULL == allocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_create() failed!\n");
		goto exit;
	}

	subAllocPtr = (uint8_t *)omrheap_allocate(heapBase, 128);
	if (NULL == subAllocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_allocate() failed!\n");
		goto exit;
	}
	walkHeap(OMRPORTLIB, heapBase, testName);
	subAllocPtrSaved = subAllocPtr;
	for (i = 0; i < 128; i++) {
		subAllocPtr[i] = (uint8_t)i;
	}
	subAllocPtr = (uint8_t *)omrheap_reallocate(heapBase, subAllocPtr, 256);
	walkHeap(OMRPORTLIB, heapBase, testName);
	if (subAllocPtr != subAllocPtrSaved) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Unexpected (subAllocPtr (%p) != subAllocPtrSaved (%p))\n",
						   subAllocPtr, subAllocPtrSaved);
		goto exit;
	}
	for (i = 0; i < 128; i++) {
		if (subAllocPtr[i] != i) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "Unexpected (subAllocPtr[i] (%d) != i (%d))\n",
							   subAllocPtr[i], i);
			goto exit;
		}
	}
	subAllocPtr2 = (uint8_t *)omrheap_allocate(heapBase, 512);
	walkHeap(OMRPORTLIB, heapBase, testName);
	if (NULL != omrheap_reallocate(heapBase, subAllocPtr, 512)) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Unexpected omrheap_reallocate() succeeded\n");
		goto exit;
	}
	walkHeap(OMRPORTLIB, heapBase, testName);
	omrheap_free(heapBase, subAllocPtr2);
	walkHeap(OMRPORTLIB, heapBase, testName);
	subAllocPtr2 = (uint8_t *)omrheap_reallocate(heapBase, subAllocPtr, 512);
	if (subAllocPtr2 != subAllocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Unexpected omrheap_reallocate() failed\n");
		goto exit;
	}
	omrmem_free_memory(allocPtr);

exit:
	reportTestExit(OMRPORTLIB, testName);
}

/**
 * Verify port library heap grow functionality.
 *
 * Tests that omrheap_grow() works as expected.
 *
 * Ensure the port library is properly setup to run heap operations.
 */
TEST(PortHeapTest, heap_grow_test)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portTestEnv->getPortLibrary());
	const char *testName = "omrheap_grow_test";

	/*malloc 2K as backing storage for the heap*/
	uintptr_t memAllocAmount = 2 * 1024;
	/* Initial heap will use 1K */
	uintptr_t initialHeapAmount = 1024;
	uintptr_t initialFirstFreeBlockValue = (sizeof(J9Heap) / sizeof(uint64_t));
	uintptr_t initialNumberOfSlots;
	uintptr_t firstFreeBlock;
	uintptr_t heapSize;
	uint64_t headTailValue;
	uint64_t *baseSlot;
	J9Heap *heapBase;
	uint8_t *allocPtr, *subAllocPtr1, *subAllocPtr2, *subAllocPtr3;
	intptr_t i;

	reportTestEntry(OMRPORTLIB, testName);

	/* Allocate backing storage using mem_allocate and pass in the returned pointer and the allocated size
	 * Verify heap integrity after each heap operation
	 */
	allocPtr = (uint8_t *)omrmem_allocate_memory(memAllocAmount, OMRMEM_CATEGORY_PORT_LIBRARY);
	if (NULL == allocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to allocate %zu bytes for the heap \n", memAllocAmount);
		goto exit;
	}

	heapBase = omrheap_create(allocPtr, initialHeapAmount, 0);
	if (NULL == heapBase) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_create() failed! \n");
		goto exit;
	}

	initialNumberOfSlots = initialHeapAmount / sizeof(uint64_t) - initialFirstFreeBlockValue;

	baseSlot = (uint64_t *)heapBase;
	/* test the integrity of the heap */
	walkHeap(OMRPORTLIB, heapBase, testName);

	/*
	 * struct J9Heap{
	 *		uintptr_t heapSize;  total size of the heap in number of slots
	 *  	uintptr_t firstFreeBlock;  slot number of the first free block within the heap
	 *  }
	 *
	 * HeapSize is represented by the number of slots in it. Each slot is the size of uint64_t which is 8 bytes.
	 * So expected heapSize is 1024/8 = 128 slots
	 * Initially when heap is created, 2*sizeof(uintptr_t) of the slots are used for heap header.
	 * So 1 slot is used for header on 32 bit platforms, 2 slots are used on 64 bit platforms.
	 * For each block of memory, either allocated or free, there is a head and tail slots.
	 * If the value in tail is negative, then it means that the block is allocated.
	 * If it is free, then it means that the block is free.
	 * Absolute value of the integer in head and tail show how many slots there are BETWEEN head and tail slots.
	 * So when omrheap created initially, firstFreeBlock always has the value 2(on 64bit) or 1 (on 32bit) and heapSize should be 128 slots in this test heap.
	 * And firstFreeBlock should point to the first slot after header
	 * which should have value 128 - sizeof(omrheap)/sizeof(uint64_t) - 2(head and tail of the slot) = 124 (on 64bit) or 125 (on 32bit)
	 *
	 *
	 * |--------|-----|--------------------------------------------------------------------------------------------------------
	 * |heapSize|first|    |    |																						 |	  |
	 * |		|Free |	124|    | ....................................all heap is free...................................| 124|
	 * |		|Block|	   |    |																						 |    |
	 * |--------|-----|-------------------------------------------------------------------------------------------------------
	 * 0        1     2	   3	4  																						 127  128
	 *     128    1|2
	 *          (32|64bit)
	 *
	 */
	if (128 != heapBase->heapSize) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_create failed to set the heap->heapSize correctly. Expected :  128, Found : %zu \n", heapBase->heapSize);
		goto exit;
	}

	if (initialFirstFreeBlockValue != heapBase->firstFreeBlock) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_create failed to set the heap->firstFreeBlock correctly. Expected :  %zu, Found : %zu \n", initialFirstFreeBlockValue, heapBase->firstFreeBlock);
		goto exit;
	}

	/* 2 slots are being used for the head and tail of the free block */
	if (initialNumberOfSlots - 2 != baseSlot[heapBase->firstFreeBlock]) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate failed to set the firstFreeBlock's head value correctly. Expected :  %zu, Found : %zd \n", initialNumberOfSlots - 2, baseSlot[heapBase->firstFreeBlock]);
		goto exit;
	}

	/*
	 * Heap is created successfully.
	 * Currently there are 128 - 2 (j9header) = 126 Slots free.
	 *
	 * + in the slot means that it is occupied/allocated by heap.
	 *
	 * 1. Allocate 64 bytes (8 slots). (Consumed 8 + 1 head + 1 tail = 10 slots) (Remaining free = 126 - 10 = 116)
	 * |--------|-----|--------------------------------------------------------------------------------------------------------
	 * |heapSize|first|    |    |		|    |	 |																		 |	  |
	 * |		|Free |	-8 |  + | ++++++| -8 |114|.......................................................................| 114|
	 * |		|Block|	   |    |		|    |	 |																		 |    |
	 * |--------|-----|-------------------------------------------------------------------------------------------------------
	 * 0        1     2    3    4       11   12                                                                         127   128
	 *     128   11|12
	 *          (32|64bit)
	 */
	subAllocPtr1 = (uint8_t *)omrheap_allocate(heapBase, 64);
	if (NULL == subAllocPtr1) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_allocate() failed!\n");
		goto exit;
	}
	/* test the integrity of the heap */
	walkHeap(OMRPORTLIB, heapBase, testName);

	if (128 != heapBase->heapSize) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate corrupted heapBase->heapSize. Expected :  128, Found : %zu \n", heapBase->heapSize);
		goto exit;
	}

	if (initialFirstFreeBlockValue + 10 != heapBase->firstFreeBlock) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate failed to set the heap->firstFreeBlock correctly. Expected :  %zu, Found : %zu \n", initialFirstFreeBlockValue + 10, heapBase->firstFreeBlock);
		goto exit;
	}

	if (initialNumberOfSlots - 12 != baseSlot[heapBase->firstFreeBlock]) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate failed to set the firstFreeBlock's head value correctly. Expected :  %zu, Found : %zd \n", initialNumberOfSlots - 10, baseSlot[heapBase->firstFreeBlock]);
		goto exit;
	}

	/* 2. Allocate the rest of the heap and make it full. So (initialNumberOfSlots-12(used above)-2(head and tail))*8 bytes need to be allocated
	* |--------|-----|--------------------------------------------------------------------------------------------------------
	* |heapSize|first|    |    |		|    |	  |																		 |	  |
	* |		|Free |	-8 |  + | ++++++| -8 |-114|++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++|-114|
	* |		|Block|	   |    |		|    |	  |																		 |    |
	* |--------|-----|-------------------------------------------------------------------------------------------------------
	* 0        1     2    3    4       11   12																			127   128
	*     128     0
	*/

	subAllocPtr2 = (uint8_t *)omrheap_allocate(heapBase, (initialNumberOfSlots - 14) * 8);
	if (NULL == subAllocPtr2) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_allocate() failed!\n");
		goto exit;
	}
	/* test the integrity of the heap */
	walkHeap(OMRPORTLIB, heapBase, testName);

	if (128 != heapBase->heapSize) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate corrupted heapBase->heapSize. Expected :  128, Found : %zu \n", heapBase->heapSize);
		goto exit;
	}

	/* firstFreeBlock is set to 0 once the heap is completely full */
	if (0 != heapBase->firstFreeBlock) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate failed to set the heap->firstFreeBlock correctly. Expected :  0, Found : %zu \n", heapBase->firstFreeBlock);
		goto exit;
	}

	/*
	 * 3. Try to allocate more memory from the full heap and
	 * make sure returned pointer is NULL meaning that heap_allocate failed
	 *
	 */

	subAllocPtr3 = (uint8_t *)omrheap_allocate(heapBase, 912);
	if (NULL != subAllocPtr3) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_allocate() failed!\n");
		goto exit;
	}
	/* test the integrity of the heap */
	walkHeap(OMRPORTLIB, heapBase, testName);

	if (128 != heapBase->heapSize) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate corrupted heapBase->heapSize. Expected :  128, Found : %zu \n", heapBase->heapSize);
		goto exit;
	}

	/* firstFreeBlock is set to 0 once the heap is completely full */
	if (0 != heapBase->firstFreeBlock) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate failed to set the heap->firstFreeBlock correctly. Expected :  0, Found : %zu \n", heapBase->firstFreeBlock);
		goto exit;
	}

	/* 4. Try to add 10 slots to the heap
	* |--------|-----|--------------------------------------------------------------------------------------------------------------------------
	* |heapSize|first|    |    |		|    |	  |																 |	  |    |   				|	 |
	* |		|Free |	-8 |  + | ++++++| -8 |-114|++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++|-114|  8 |................| 8  |
	* |		|Block|	   |    |		|    |	  |																 |    |    | 				|	 |
	* |--------|-----|--------------------------------------------------------------------------------------------------------------------------
	* 0        1     2    3    4       11   12	  13											     			127   128  129              137  138
	*    138     128
	*
	*  Since the heap was completely full, growing should update following:
	*  a. firstFreeBlock should be set to 128 which points to the beginning of newly added slots.
	*  b. heapSize should be incremented 10 and be set to 138
	*  c. The head of newly added free block of slots should have the value 8. Since 10 slots are added, 2 slots are used for head and tail.
	*/

	if (TRUE != omrheap_grow(heapBase, 80)) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_grow() failed!\n");
		goto exit;
	}

	/* test the integrity of the heap */
	walkHeap(OMRPORTLIB, heapBase, testName);

	if (138 != heapBase->heapSize) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate corrupted heapBase->heapSize. Expected :  138, Found : %zu", heapBase->heapSize);
		goto exit;
	}

	if (128 != heapBase->firstFreeBlock) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_grow() failed to set firstFreeBlock correctly. Expected value = 128, Found : %zu \n", heapBase->firstFreeBlock);
		goto exit;
	}

	if (8 != baseSlot[heapBase->firstFreeBlock]) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate failed to set the firstFreeBlock's head value correctly. Expected :  8, Found : %zd", baseSlot[heapBase->firstFreeBlock]);
		goto exit;
	}

	if (8 != baseSlot[heapBase->heapSize - 1]) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "heap_allocate failed to set the firstFreeBlock's tail value correctly. Expected :  8, Found : %zd", baseSlot[heapBase->firstFreeBlock]);
		goto exit;
	}

	/* Recursively grow heap.
	 * Since the last slots of the heap has just added and they are empty,
	 * it is expected that newly added slots should merge with the current heap's last slots.
	 * In this case, here is what is expected:
	 * firstFreeBlock never changes value.
	 * omrheap->heapSize size grows
	 * firstFreeBlock points to the head of last slots and the value of head changes as we grow heap. So do tail.
	 *
	 */
	firstFreeBlock = heapBase->firstFreeBlock;
	headTailValue = baseSlot[heapBase->firstFreeBlock];
	heapSize = heapBase->heapSize;
	for (i = 0; i < 20; i++) {
		/* 40 bytes = 5 slots*/
		if (TRUE != omrheap_grow(heapBase, 40)) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_grow() failed!\n");
			goto exit;
		}

		/* test the integrity of the heap */
		walkHeap(OMRPORTLIB, heapBase, testName);

		if (firstFreeBlock != heapBase->firstFreeBlock) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_grow() failed!\n");
			goto exit;
		}

		if (heapBase->heapSize != heapSize + (i + 1) * 5) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_grow() failed!\n");
			goto exit;
		}

		if (baseSlot[heapBase->firstFreeBlock] != headTailValue + (i + 1) * 5) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_grow() failed!\n");
			goto exit;
		}

		if (baseSlot[heapBase->heapSize - 1] != headTailValue + (i + 1) * 5) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "omrheap_grow() failed!\n");
			goto exit;
		}
	}


	omrmem_free_memory(allocPtr);

exit:
	reportTestExit(OMRPORTLIB, testName);
}

/**
 * For details:
 * JAZZ103 72449
 * JAZZ103 71328
 *
 * In order to create the scenario in this PMR, heap's different segments need to be allocated and freed at a certain order.
 *
 * This is the scenario where lastAllocSlot is not updated correctly and it points to somewhere in the middle of a segment and has the value of 0.
 *
 *
 */
TEST(PortHeapTest, heap_test_pmr_28277_999_760)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portTestEnv->getPortLibrary());
	const char *testName = "omrheap_test_pmr_28277_999_760";

	uintptr_t heapSize = 800; /* Big enough */
	J9Heap *heapBase;
	uint8_t *allocPtr = NULL;

	void *alloc1 = NULL;
	void *alloc4_1 = NULL;
	void *alloc4_2 = NULL;

	reportTestEntry(OMRPORTLIB, testName);

	allocPtr = (uint8_t *)omrmem_allocate_memory(heapSize, OMRMEM_CATEGORY_PORT_LIBRARY);
	if (NULL == allocPtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Failed to allocate %zu bytes for the heap\n", heapSize);
		goto exit;
	} else {
		portTestEnv->log("\tallocPtr: 0x%p\n", allocPtr);
	}

	memset(allocPtr, 0xff, heapSize);

	heapBase = omrheap_create(allocPtr, heapSize, 0);

	/*
	 * struct J9Heap{
	 *  	uintptr_t heapSize;
	 *  	uintptr_t firstFreeBlock;
	 *  	uintptr_t lastAllocSlot;
	 *  	uintptr_t largestAllocSizeVisited;
	 *	};

	 * After heap is created.*
	 *
	 * |	    HEAP HEADER					   |
	 * |--------------------------------------|------------------------------------------------------------------------------------------------
	 * |heapSize|first   | last    | largest  |  |																							|	|
	 * |		 |Free    | Alloc	|  Alloc   |  |																							|	|
	 * |		 |Block   | Slot	|  Size    |94|			FREE			FREE			FREE					FREE					|94	|
	 * |		 |        |     	|  Visited |  |																							|	|
	 * |	100	 |  4     |  4   	|   94     |  |																							|	|
	 * |--------|--------|---------|----------|--|-----------------------------------------------------------------------------------------|---|
	 * 0        1        2         3          4  5     																					99		100
	 *
	 *FYI : Each segment is the size of uint64_t
	 */


	if (NULL == heapBase) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "failed to create the heap\n");
		goto exit;
	}

	walkHeap(OMRPORTLIB, heapBase, testName);

	/*
	 * Following series of omrheap function calls is to create the scenario in the mentioned PMRs above.
	 *
	 * Illustration of the heap after every call is before this PMR fixed!!!
	 *
	 * With the fix, firstFree and lastAllocSlot values should point to the right segments of heap. This test verifies they do.
	 *
	 */

	omrheap_allocate(heapBase, 16); /* 16/8 = 2   Alloc 2 slots. Slot Size = sizeof(uint64_t) */
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|	|	|														  								     					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|	|	|																	     										|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2	|90	|   FREE			FREE			FREE					FREE			FREE			FREE				|90	|
	 * |		|       |	   	|  Visited|	 |2Slots|	|	|																												|   |
	 * |	100	|  8    |  8    |   0     |	 |		|	|	|																												|   |
	 * |--------|-------|-------|---------|--|------|---|---|---------------------------------------------------------------------------------------------------------------|---|
	 * 0        1       2       3         4  5  	7	8	9																			99	100
	 *
	 */


	alloc1 = omrheap_allocate(heapBase, 16);  /* 16/8 = 2   Alloc 2 slots. Slot Size = sizeof(uint64_t) */
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |																									|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |															     									|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2|-2|Alloc1|-2|86|   FREE			FREE			FREE				FREE			FREE			FREE			|86	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |    																								|   |
	 * |	100	|  12   |  12   |   0     |	 |		|  |  |		 |  |  |																									|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|----------------------------------------------------------------------------------------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11	12 13		     																						99	100
	 *
	 */


	omrheap_allocate(heapBase, 16); /* 16/8 = 2   Alloc 2 slots. Slot Size = sizeof(uint64_t) */
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |																						|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |												     									|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2|-2|Alloc1|-2|-2|Alloc2|-2|82|	FREE			FREE				FREE			FREE			FREE			|82	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  | 																						|   |
	 * |	100	|  16   |  16   |   0     |	 |		|  |  |		 |  |  |	  |  |  |																						|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|---------------------------------------------------------------------------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17    																					99	100
	 *
	 */

	omrheap_allocate(heapBase, 16); /* 16/8 = 2   Alloc 2 slots. Slot Size = sizeof(uint64_t)*/
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |																			|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |												     						|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2|-2|Alloc1|-2|-2|Alloc2|-2|-2|Alloc |-2|78|	FREE			FREE				FREE			FREE		FREE	|78	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |											     							|   |
	 * |	100	|  20   |  20   |   0     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |																			|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|--------------------------------------------------------------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21																			99	100
	 *
	 */

	alloc4_1 = omrheap_allocate(heapBase, 64);  /* 64/8 = 8   Alloc 8 Slots. Slot Size = sizeof(uint64_t)*/
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |												|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                 		|  |  |											 	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2|-2|Alloc1|-2|-2|Alloc2|-2|-2|Alloc3|-2|-2|Alloc4_1         		|-2|68|			FREE				FREE			|68	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8Slots            	|  |  |												|   |
	 * |	100	|  30   |  30   |   0     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |                  	|  |  |												|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|---------------------------------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31										   99	100
	 *
	 */


	omrheap_allocate(heapBase, 16);  /* 16/8 = 2   Alloc 2 slots. Slot Size = sizeof(uint64_t) */
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |								|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |						     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2|-2|Alloc1|-2|-2|Alloc2|-2|-2|Alloc3|-2|-2|Alloc4_1          	|-2|-2|Alloc5|-2|64|				FREE			|64	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8Slots            	|  |  |2Slots|  |  |					     		|   |
	 * |	100	|  34   |  34   |   0     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |                  	|  |  |      |  |  |								|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|------|--|--|--------------------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31     33 34 35    							99	100
	 *
	 */

	omrheap_allocate(heapBase, 16);  /* 16/8 = 2   Alloc 2 slots. Slot Size = sizeof(uint64_t) */
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2|-2|Alloc1|-2|-2|Alloc2|-2|-2|Alloc3|-2|-2|Alloc4_1          	|-2|-2|Alloc5|-2|-2|Alloc6|-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8Slots            	|  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|  38   |  38   |   0     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |                  	|  |  |      |  |  |      |  |  | 					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|------|--|--|------|--|--|-------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31     33 34 35     37 38 39					99	100
	 *
	 */

	walkHeap(OMRPORTLIB, heapBase, testName); /* Just a sanity check */

	omrheap_free(heapBase, alloc1);
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |              	    |  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |              	    |  |  |      |  |  |      |  |  |			    	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2| 2|FREE  | 2|-2|Alloc2|-2|-2|Alloc3|-2|-2|Alloc4_1      	    |-2|-2|Alloc5|-2|-2|Alloc6|-2|60|		FREE		|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8Slots        	    |  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8   |   8   |   0     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |              	    |  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|------|--|--|------|--|--|-------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31     33 34 35     37 38 39   				99	100
	 *
	 */

	omrheap_free(heapBase, alloc4_1);
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                 		|  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2| 2|FREE  | 2|-2|Alloc2|-2|-2|Alloc3|-2|8 |FREE              	|8 |-2|Alloc |-2|-2|Alloc |-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8slots            	|  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8   |   8   |   0     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |                  	|  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|------|--|--|------|--|--|-------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31     33 34 35     37 38 39					99	100
	 *
	 */

	alloc4_1 = omrheap_allocate(heapBase, 24);  /* 24/8 = 3   Alloc 3 slots. Slot Size = sizeof(uint64_t) */
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |        |  |  |        |  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |        |  |  |        |  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc	|-2| 2|FREE  | 2|-2|Alloc |-2|-2|Alloc |-2|-3|Alloc4_1|-3|3 |FREE    |3 |-2|Alloc |-2|-2|Alloc |-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  | 3Slots |  |  |3Slots  |  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8   |   25  |   2     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |        |  |  |        |  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|--------|--|--|--------|--|--|------|--|--|------|--|--|-- ---------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	     24 25 26        29 30 31     33 34 35   	37 38 39 				99	100
	 *
	 */

	alloc4_2 = omrheap_allocate(heapBase, 24);  /* 24/8 = 3   Alloc 3 slots. Slot Size = sizeof(uint64_t) */
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |        |  |  |        |  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |        |  |  |        |  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc	|-2| 2|FREE  | 2|-2|Alloc |-2|-2|Alloc |-2|-3|Alloc4_1|-3|-3|Alloc4_2|-3|-2|Alloc |-2|-2|Alloc |-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  | 3Slots |  |  |3Slots  |  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8   |   25  |   2     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |        |  |  |        |  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|--------|--|--|--------|--|--|------|--|--|------|--|--|-- ---------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	     24 25 26        29 30 31     33 34 35   	37 38 39 				99	100
	 *
	 */

	omrheap_free(heapBase, alloc4_1);
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |        |  |  |        |  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |        |  |  |        |  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc	|-2| 2|FREE  | 2|-2|Alloc |-2|-2|Alloc |-2| 3|FREE	  | 3|-3|Alloc4_2|-3|-2|Alloc |-2|-2|Alloc |-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  | 3Slots |  |  |3Slots  |  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8   |   25  |   3     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |        |  |  |        |  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|--------|--|--|--------|--|--|------|--|--|------|--|--|-- ---------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	     24 25 26        29 30 31     33 34 35   	37 38 39 				99	100
	 *
	 */

	omrheap_free(heapBase, alloc4_2);
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                 		|  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2| 2|FREE  | 2|-2|Alloc2|-2|-2|Alloc3|-2|8 |FREE              	|8 |-2|Alloc |-2|-2|Alloc |-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8slots            	|  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8	|   25  |   8     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |                  	|  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|------|--|--|------|--|--|-------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31     33 34 35     37 38 39					99	100
	 *
	 *
	 * PROBLEM: As you can see above, lastAllocSlot is not pointing to the beginning of alloc/free segment. It is in the middle of the free segment.
	 *
	 * If this segment is allocated again, and value is written in it, then lastAllocSlot will have random values. See below calls.
	 *
	 */


	alloc4_1 = omrheap_allocate(heapBase, 48);
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                 		|  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2| 2|FREE  | 2|-2|Alloc2|-2|-2|Alloc3|-2|-8|Alloc4_1            	|-8|-2|Alloc |-2|-2|Alloc |-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8slots            	|  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8	|   25  |   8     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |                  	|  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|------|--|--|------|--|--|-------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31     33 34 35     37 38 39					99	100
	 *
	 */

	memset(alloc4_1, 0, 48);

	omrheap_free(heapBase, alloc4_1);
	/*
	 *
	 *
	 * |	    HEAP HEADER               |
	 * |----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------
	 * |heapSize|first  | last  | largest |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                 		|  |  |      |  |  |      |  |  |					|   |
	 * |		|Free   | Alloc |  Alloc  |	 |		|  |  |		 |  |  |	  |  |  |	   |  |  |                  	|  |  |      |  |  |      |  |  |			     	|   |
	 * |		|Block  | Slot	|  Size   |-2|Alloc0|-2| 2|FREE  | 2|-2|Alloc2|-2|-2|Alloc3|-2|8 |FREE              	|8 |-2|Alloc |-2|-2|Alloc |-2|60|	FREE			|60	|
	 * |		|       |	   	|  Visited|	 |2Slots|  |  |2Slots|  |  |2Slots|  |  |2Slots|  |  |8slots            	|  |  |2Slots|  |  |2Slots|  |  |		     		|   |
	 * |	100	|   8	|   25  |   8     |	 |		|  |  |		 |  |  |	  |  |  |      |  |  |                  	|  |  |      |  |  |      |  |  |					|   |
	 * |--------|-------|-------|---------|--|------|--|--|------|--|--|------|--|--|------|--|--|----------------------|--|--|------|--|--|------|--|--|-------------------|---|
	 * 0        1       2       3         4  5  	7  8  9	 	11 12 13     15 16 17	  19 20 21	               		29 30 31     33 34 35     37 38 39					99	100
	 *
	 */


	/*
	 * 100 bytes is big enough to start from lastAllocSlot  (100 > largestAllocSizeVisited)
	 * lastAllocSlot should be bogus and should have value 0 before the fix.
	 *
	 */
	omrheap_allocate(heapBase, 100);

exit:
	omrmem_free_memory(allocPtr);
	reportTestExit(OMRPORTLIB, testName);
}


/**
 * Helper function that iterates all used elements in the pool and call heap_free to free each memory blocks, whose information is stored in a pool element.
 */
static void
freeRemainingElementsInPool(struct OMRPortLibrary *portLibrary, J9Heap *heapBase, J9Pool *allocPool)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portLibrary);
	pool_state state;
	AllocListElement *element = (AllocListElement *)pool_startDo(allocPool, &state);

	while (NULL != element) {
		omrheap_free(heapBase, element->allocPtr);
		element = (AllocListElement *)pool_nextDo(&state);
	}
}

/**
 * Helper function determines the largest size one can allocate in a empty J9Heap.
 * It tries to call heap_allocate with the size used to create the heap, and gradually decreases the size until an heap_allocate succeeds.
 * It returns the size of the largest possible allocation.
 */
static uintptr_t
allocLargestChunkPossible(struct OMRPortLibrary *portLibrary, J9Heap *heapBase, uintptr_t heapSize)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portLibrary);
	uintptr_t allocSize = heapSize;
	void *allocMem = NULL;

	do {
		allocMem = omrheap_allocate(heapBase, allocSize);
		if (NULL != allocMem) {
			omrheap_free(heapBase, allocMem);
			return allocSize;
		}
		allocSize -= 1;
	} while (allocSize > 0);
	return 0;
}

/**
 * Helper function that removes a specific element from the pool.
 * The element to be removed is the removeIndex'th one when we iterate the used elements in pool.
 * Returns the AllocListElement->allocPtr stored in the pool element.
 */
static void *
removeItemFromPool(struct OMRPortLibrary *portLibrary, J9Pool *allocPool, uintptr_t removeIndex)
{
	uintptr_t i = 0;
	pool_state state;
	void *subAllocPtr = NULL;
	AllocListElement *element = (AllocListElement *)pool_startDo(allocPool, &state);

	for (i = 0; i < removeIndex; i++) {
		element = (AllocListElement *)pool_nextDo(&state);
	}
	subAllocPtr = element->allocPtr;
	pool_removeElement(allocPool, element);

	return subAllocPtr;
}

/**
 * Helper function that gets a specific element from the pool.
 */
static AllocListElement *
getElementFromPool(struct OMRPortLibrary *portLibrary, J9Pool *allocPool, uintptr_t index)
{
	uintptr_t i = 0;
	pool_state state;
	AllocListElement *element = (AllocListElement *)pool_startDo(allocPool, &state);

	for (i = 0; i < index; i++) {
		element = (AllocListElement *)pool_nextDo(&state);
	}

	return element;
}

/**
 * validate sub-allocated memory pointer returned by heap_allocate.
 */
static void
verifySubAllocMem(struct OMRPortLibrary *portLibrary, void *subAllocMem, uintptr_t allocSize, J9Heap *heapBase, const char *testName)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portLibrary);
	uint64_t *basePtr = (uint64_t *)heapBase;
	uintptr_t heapSize = ((uintptr_t *)basePtr)[0];
	I_64 *blockStart = (I_64 *)subAllocMem;
	I_64 blockSizeStart = blockStart[-1];
	I_64 blockSizeEnd = 0;

	/* verify the mem ptr is aligned */
	if (((uintptr_t)subAllocMem) & 7) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Sub-allocated memory is not 8-aligned: 0x%p\n", subAllocMem);
		return;
	}

	/* verify the mem ptr is within the range of heap */
	if ((subAllocMem < (void *)&basePtr[SIZE_OF_J9HEAP_HEADER / sizeof(uint64_t)]) || (subAllocMem > (void *)&basePtr[heapSize - 1])) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Sub-allocated memory is not within the valid range of heap: 0x%p\n", subAllocMem);
		return;
	}

	/* verify the mem ptr is the first slot of an empty block and the block size should not be overly large */
	if (blockSizeStart >= 0) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Sub-allocated memory is not part of an occupied block: 0x%p, top size: %lld, bottom size: %lld\n", subAllocMem, blockSizeStart, blockSizeEnd);
		return;
	}
	blockSizeEnd = blockStart[-blockSizeStart];
	if (blockSizeStart != blockSizeEnd) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "Top and bottom padding of the Sub-allocated memory do not match: 0x%p\n", subAllocMem);
		return;
	}
	if (0 == allocSize) {
		if (!(1 <= -blockSizeStart && -blockSizeStart <= 3)) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "The allocated block seems to be overly large (3 or more free slots): 0x%p, request size: %zu, block size: %lld\n", subAllocMem, allocSize, blockSizeStart);
			return;
		}
	} else {
		if (!(allocSize <= ((uintptr_t)(-blockSizeStart)*sizeof(uint64_t)) && ((uintptr_t)(-blockSizeStart)*sizeof(uint64_t)) < (allocSize + 3 * sizeof(uint64_t)))) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "The allocated block seems to be overly large (3 or more free slots): 0x%p, request size: %zu, block size: %lld\n", subAllocMem, allocSize, blockSizeStart);
			return;
		}
	}
}

/**
 * helper function that iterates the used elements in a Pool and produces some verbose output of the information stored in AllocListElement.
 */
static void
iteratePool(struct OMRPortLibrary *portLibrary, J9Pool *allocPool)
{
	pool_state state;
	AllocListElement *element;

	if (0 == pool_numElements(allocPool)) {
		portTestEnv->log(LEVEL_VERBOSE, "Pool has become empty");
	} else {
		element = (AllocListElement *)pool_startDo(allocPool, &state);
		do {
			portTestEnv->log(LEVEL_VERBOSE, "[%zu] ", element->allocSerialNumber);
			element = (AllocListElement *)pool_nextDo(&state);
		} while (NULL != element);
	}
	portTestEnv->log(LEVEL_VERBOSE, "\n\n");
}

/**
 * Verifies that we did not touch any of the memory immediately before and after the heap.
 * We check for memory location from memAllocStart to memAllocStart+heapStartOffset, which immediate precedes the heap.
 * Also check for the next 500 bytes after the heap.
 */
static void
verifyHeapOutofRegionWrite(struct OMRPortLibrary *portLibrary, uint8_t *memAllocStart, uint8_t *heapEnd, uintptr_t heapStartOffset, const char *testName)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portLibrary);
	uintptr_t i;

	/* ensure the memory location after the heap is not corrupted*/
	for (i = 0; i <= 500; i++) {
		if (0xff != *(heapEnd + i)) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "Address %p following the heap seems to have been modified. value: %x\n", heapEnd + i, *(heapEnd + i));
			return;
		}
	}
	/* ensure the memory location before the heap is not corrupted*/
	for (i = 0; i < heapStartOffset; i++) {
		if (0xff != *(memAllocStart + i)) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "Address %p before the heap seems to have been modified. value: %x\n", memAllocStart + i, *(memAllocStart + i));
			return;
		}
	}
}

/*
 * The walkHeap routine knows the internal structure of the heap to better test its integrity
 */
static void
walkHeap(struct OMRPortLibrary *portLibrary, J9Heap *heapBase, const char *testName)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portLibrary);
	uint64_t *basePtr = (uint64_t *)heapBase;
	uintptr_t heapSize, firstFreeBlock;
	uint64_t *lastSlot, *blockTopPaddingCursor;
	BOOLEAN firstFreeUnmatched = TRUE;
	portTestEnv->changeIndent(1);

	if (NULL == basePtr) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "\nHeap base is NULL\n");
		return;
	} else if (((uintptr_t)basePtr) & 7) {
		outputErrorMessage(PORTTEST_ERROR_ARGS, "\nHeap is not 8-aligned\n");
		return;
	}

	/*the fields of J9Heap is uintptr_t*/
	heapSize = ((uintptr_t *)basePtr)[0];
	firstFreeBlock = ((uintptr_t *)basePtr)[1];

	lastSlot = &basePtr[heapSize - 1];
	blockTopPaddingCursor = &basePtr[SIZE_OF_J9HEAP_HEADER / sizeof(uint64_t)];

	portTestEnv->log(LEVEL_VERBOSE, "J9Heap @ 0x%p: ", heapBase);
	portTestEnv->log(LEVEL_VERBOSE, "%zu|", heapSize);
	portTestEnv->log(LEVEL_VERBOSE, "%zu|", firstFreeBlock);

	while (((uintptr_t)blockTopPaddingCursor) < ((uintptr_t)lastSlot)) {
		I_64 topBlockSize, bottomBlockSize, absSize;
		uint64_t *blockBottomPaddingCursor;

		absSize = topBlockSize = *blockTopPaddingCursor;

		if (0 == topBlockSize) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "\nThe block size is 0 @ 0x%p\n", blockTopPaddingCursor);
			return;
		} else if (topBlockSize < 0) {
			absSize = -topBlockSize;
		} else {
			if (0 == firstFreeBlock) {
				outputErrorMessage(PORTTEST_ERROR_ARGS, "\nfirstFreeBlock indicates heap is full but found empty block @ 0x%p\n", blockTopPaddingCursor);
				return;
			}
			if (firstFreeUnmatched == TRUE) {
				if (firstFreeBlock != (((uintptr_t)blockTopPaddingCursor - (uintptr_t)basePtr) / sizeof(uint64_t))) {
					outputErrorMessage(PORTTEST_ERROR_ARGS, "\nfirstFreeBlock and the actual fist free block don't match @ 0x%p\n", blockTopPaddingCursor);
					return;
				}
				firstFreeUnmatched = FALSE;
			}
		}

		blockBottomPaddingCursor = blockTopPaddingCursor + (absSize + 1);
		bottomBlockSize = (I_64) * blockBottomPaddingCursor;

		if (topBlockSize != bottomBlockSize) {
			outputErrorMessage(PORTTEST_ERROR_ARGS, "\nsize in top and bottom block padding don't match @ 0x%p\n", blockTopPaddingCursor);
			return;
		}
		portTestEnv->log(LEVEL_VERBOSE, "%lld|", topBlockSize);
		blockTopPaddingCursor = blockBottomPaddingCursor + 1;
	}
	portTestEnv->log(LEVEL_VERBOSE, "\n");
	portTestEnv->changeIndent(-1);
}

/**
 * Verify port library heap operations.
 *
 * Exercise all API related to port library heap operations found in
 * @ref omrheap.c
 */
TEST(PortHeapTest, heap_test2)
{
	OMRPORT_ACCESS_FROM_OMRPORT(portTestEnv->getPortLibrary());
	int rc = 0;
	int i;
	I_64 timeStart = 0;
	I_64 testDurationMin = 5;
	int randomSeed = 0;
	char srand[99] = "";

	for (i = 1; i < portTestEnv->_argc; i += 1) {
		if (startsWith(portTestEnv->_argv[i], (char *)"-srand:")) {
			strcpy(srand, &portTestEnv->_argv[i][7]);
			randomSeed = atoi(srand);
		}
	}
	/* Display unit under test */
	HEADING(OMRPORTLIB, "Heap test");

	/* verify sanity of port library for these tests.  If this fails there is no
	 * point in continuing
	 */
	timeStart = omrtime_current_time_millis();
	for (i = 0; i < 2; i += 1) {
		rc |= omrheap_test2(OMRPORTLIB, randomSeed);
		if (rc) {
			portTestEnv->log(LEVEL_ERROR, "omrheap_test2 failed loop %d\n\n", i);
			break;
		} else {
			portTestEnv->log("omrheap_test2 passed loop %d.\n", i);
			/* we check for time here and terminate this loop if we've run for too long */
			if ((omrtime_current_time_millis() - timeStart) >= testDurationMin * 60 * 1000) {
				portTestEnv->log("We've run for too long, ending the test. This is not a test failure.\n\n");
				break;
			}
			portTestEnv->log("\n\n");
		}
	}
	/* Output results */
	portTestEnv->log("\nHeap test done%s\n\n", rc == TEST_PASS ? "." : ", failures detected.");
	EXPECT_TRUE(TEST_PASS == rc) << "Test Failed!";
}
