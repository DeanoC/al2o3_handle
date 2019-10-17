// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_handle/dynamic.h"

AL2O3_FORCE_INLINE bool IsPow2(uint32_t num) {
	return ((num & (num - 1)) == 0);
}

AL2O3_FORCE_INLINE uint32_t NextPow2(uint32_t num) {
	num -= 1;
	num |= num >> 16;
	num |= num >> 8;
	num |= num >> 4;
	num |= num >> 2;
	num |= num >> 1;

	return num + 1;
}

// assumes power of 2
AL2O3_FORCE_INLINE uint32_t SlowLog2(uint32_t num) {
	if (num == 0) {
		return 0;
	}
	uint32_t count = 0;
	do {
		num >>= 1;
		count++;
	} while ((num & 0x1) == 0);
	return count;
}

AL2O3_FORCE_INLINE bool Handle_DynamicManager32IsValid(Handle_DynamicManager32 *manager,
																											 Handle_DynamicHandle32 handle) {
	uint32_t const handleGen = handle >> 24;
	uint32_t index = (handle & Handle_MaxDynamicHandles32);

	// fetch the base memory block for this index
	uint8_t *base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[index >> manager->handlesPerBlockShift]);
	ASSERT(base);
	index = index & manager->handlesPerBlockMask;
	// point to generation data for this index
	uint8_t *gen = base + ((manager->handlesPerBlockMask + 1) * manager->elementSize) + index;

	return (handleGen == *gen);
}

AL2O3_FORCE_INLINE void *Handle_DynamicManager32HandleToPtr(Handle_DynamicManager32 *manager,
																														Handle_DynamicHandle32 handle) {
	// fetch the base memory block for this index
	uint32_t const index = (handle & Handle_MaxDynamicHandles32);
	uint8_t const * const base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[index >> manager->handlesPerBlockShift]);
	ASSERT(base);
	// check the generation
	uint8_t const * const gen = base + ((manager->handlesPerBlockMask + 1) * manager->elementSize) + (index & manager->handlesPerBlockMask);
//	ASSERT((handle >> 24) == *gen);
	if((handle >> 24) != *gen) {
		return NULL;
	}
	return (void*)(base + ((index & manager->handlesPerBlockMask) * manager->elementSize));
}

// return true to retry the allocation, false means no hope
static bool AllocNewBlock(Handle_DynamicManager32 *manager) {
	if (Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated) >= Handle_MaxDynamicHandles32) {
		LOGWARNING("Allocated all 16.7 million handles already!");
		return false;
	}
	if (Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated)
			>= (manager->handlesPerBlockMask + 1) * Handle_DynamicManagerMaxBlocks) {
		LOGWARNING("Trying to allocate more than %i blocks! Increase block size", Handle_DynamicManagerMaxBlocks);
		return false;
	}

	// first thing we need to do is claim our new index range
	uint32_t baseIndex =
			Thread_AtomicFetchAdd32Relaxed(&manager->totalHandlesAllocated, (manager->handlesPerBlockMask + 1));

	size_t const blockSize = ((manager->handlesPerBlockMask + 1) * manager->elementSize) +
			((manager->handlesPerBlockMask + 1) * sizeof(uint8_t));

	uint8_t *base = (uint8_t *) MEMORY_CALLOC(1, blockSize);
	if (!base) {
		LOGWARNING("Out of memory!");
		return false;
	}

	Thread_AtomicStorePtrRelaxed(manager->blocks + (baseIndex >> manager->handlesPerBlockShift), base);

	// init free list for new block
	for (uint32_t i = 0u; i < (manager->handlesPerBlockMask+1); ++i) {
		uint32_t const index = baseIndex + i;
		uint32_t *addr = (uint32_t *) (base + (i * manager->elementSize));
		// add marker and point to next entry
		*addr = 0xFF000000u | (index + 1);
	}

	// link the new block into the free list and attach existing free list to the
	// end of this block
	RedoD0:;
	uint64_t const heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
	uint32_t const headsFreePart = (uint32_t) (heads & 0xFFFFFFFFull);
	uint64_t const headsDeferFreePart = heads & ~0xFFFFFFFFull;

	// we chain to the next entry in the free list without disturbing the deferred list
	uint64_t const newHeads = headsDeferFreePart | baseIndex;
	// point last new handle to existing free list (it might not be invalid by now)
	*((uint32_t *) (base + (manager->handlesPerBlockMask * manager->elementSize))) = headsFreePart;

	if (Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newHeads) != heads) {
		goto RedoD0; // something changed reverse the transaction
	}

	return true;
}

AL2O3_EXTERN_C Handle_DynamicManager32 *Handle_DynamicManager32Create(uint32_t elementSize, uint32_t handlesPerBlock) {
	ASSERT(elementSize >= sizeof(uint32_t));
	ASSERT(handlesPerBlock <= Handle_MaxDynamicHandles32);

	if (!IsPow2(handlesPerBlock)) {
		LOGWARNING("handlesPerBlock (%u) should be a power of 2, using %u", handlesPerBlock, NextPow2(handlesPerBlock));
		handlesPerBlock = NextPow2(handlesPerBlock);
	}

	// each block has space for the data, the generation and the index into blocks for the base pointer
	size_t const blockSize = (handlesPerBlock * elementSize) +
			(handlesPerBlock * sizeof(uint8_t));

	// first block is attached directly to the header
	size_t const allocSize = sizeof(Handle_DynamicManager32);

	Handle_DynamicManager32 *manager = (Handle_DynamicManager32 *) MEMORY_CALLOC(1, allocSize);
	if (!manager) {
		return NULL;
	}
	manager->elementSize = elementSize;
	manager->handlesPerBlockMask = handlesPerBlock - 1;
	manager->handlesPerBlockShift = SlowLog2(handlesPerBlock);

	uint8_t *base = (uint8_t *) MEMORY_CALLOC(1, blockSize);
	if(!base) {
		MEMORY_FREE(manager);
		return NULL;
	}
	Thread_AtomicStorePtrRelaxed(manager->blocks + 0, base);
	Thread_AtomicStore32Relaxed(&manager->totalHandlesAllocated, handlesPerBlock);

	memset(base, 0xDC, handlesPerBlock * manager->elementSize);

	// init free list for new block
	// both gen and block index are zero'ed via calloc
	for (uint32_t i = 0u; i < handlesPerBlock; ++i) {
		uint32_t const index = i;
		void *addr = base + (i * manager->elementSize);
		*((uint32_t *) addr) = 0xFF000000u | (index + 1);
	}

	// index zero is born generation 1
	*(base + (handlesPerBlock * manager->elementSize)) = 1;

	// fix last index to point to the invalid marker
	*((uint32_t *) (base + ((handlesPerBlock - 1) * manager->elementSize))) = 0;

	// repoint heads to start of the free list with an empty deferred list
	Thread_AtomicStore64Relaxed(&manager->freeListHeads, 0xFF000000ull);

	return manager;
}

AL2O3_EXTERN_C void Handle_DynamicManager32Destroy(Handle_DynamicManager32 *manager) {
	if (!manager) {
		return;
	}

	for (uint32_t i = 0u; i < Handle_DynamicManagerMaxBlocks; ++i) {
		void* ptr = Thread_AtomicLoadPtrRelaxed(&manager->blocks[i]);
		if(ptr) {
			MEMORY_FREE(ptr);
		}
	}

	MEMORY_FREE(manager);
}

AL2O3_EXTERN_C Handle_DynamicHandle32 Handle_DynamicManager32Alloc(Handle_DynamicManager32 *manager) {
	uint32_t noFreeCount = 0;
	RedoD0:;
	// heads has 2 linked list packed in a 64 bit location. Its our transaction
	// backout test as well
	uint64_t const heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
	uint32_t const headsFreePart = (uint32_t) (heads & 0xFFFFFFFFull);
	uint64_t const headsDeferFreePart = heads & ~0xFFFFFFFFull;

	// check to see if the free list is empty
	if (headsFreePart == Handle_InvalidDynamicHandle32) {
		// we need to swap the deferred into the free list as free list is empty
		if (headsDeferFreePart == (uint64_t) Handle_InvalidDynamicHandle32) {
			// the deferred list is empty, so we have no free handles
			// we now do the tricky part of allocating a new block in a lock free
			// way *gulp*
			bool retry = AllocNewBlock(manager);
			if (retry == false || noFreeCount >= 1000) {
				LOGWARNING("Manager has run out of handles");
				return Handle_InvalidDynamicHandle32; // fail
			}
			// try again but mark we've tried, allow a few attempts then give up
			noFreeCount++;
			goto RedoD0;
		} else {
			uint64_t const newheads = headsDeferFreePart >> 32u;
			// we move the into the free list position and mark the deferred as empty
			// we don't even have to loop here as a transaction reverse is the same thing
			Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newheads);
			goto RedoD0; // retry now
		}
	}
	// we chain to the next entry in the free list without disturbing the deferred list
	uint32_t index = headsFreePart & 0x00FFFFFF; // clean up the marker
	uint32_t const actualIndex = index;
	// fetch the base memory block for this index
	uint8_t * const base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[index >> manager->handlesPerBlockShift]);
	index = index & manager->handlesPerBlockMask;
	uint32_t * const item = (uint32_t *) (base + (index * manager->elementSize));
	ASSERT((uint8_t*)item < (base + ((manager->handlesPerBlockMask+1)*manager->elementSize) ));

	uint64_t const newHeads = headsDeferFreePart | *item;

	if (Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newHeads) != heads) {
		goto RedoD0; // something changed reverse the transaction
	}

	// the item is now ours to abuse
	// clear it out ready for its new life
	memset(item, 0xDC, manager->elementSize);

	// now make the handle and return it
	// point to generation data for this index
	uint8_t *gen = base + ((manager->handlesPerBlockMask+1) * manager->elementSize) + index;
	return actualIndex | ((uint32_t) *gen) << 24u;
}

AL2O3_EXTERN_C void Handle_DynamicManager32Release(Handle_DynamicManager32 *manager, Handle_DynamicHandle32 handle) {
	ASSERT((handle & Handle_MaxDynamicHandles32) < Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated));
	ASSERT(Handle_DynamicManager32IsValid(manager, handle));

	uint32_t index = handle & 0x00FFFFFF; // clean out the current generation
	uint32_t const actualIndex = index;

	// fetch the base memory block for this index
	uint8_t *base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[index >> manager->handlesPerBlockShift]);
	index = (index & manager->handlesPerBlockMask);
	// point to generation data for this index
	uint8_t *gen = base + ((manager->handlesPerBlockMask+1) * manager->elementSize) + index;
	uint32_t *item = (uint32_t *) (base + (index * manager->elementSize));

	// update the generation of this index
	// intentional 8 bit integer overflow
	*gen = *gen + 1;
	if (*gen == 0 && index == 0) {
		*gen = 1;
	}

	uint64_t markerIndex = ((uint64_t) actualIndex | 0xFF000000ull) << 32ull; // marker

	RedoF:;
	// add it to the deferred list without changing the free list
	// repeat until we get a transaction okay response from CAS
	uint64_t const heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
	uint64_t const headsFreePart = heads & 0xFFFFFFFFull;
	uint32_t const headsDeferFreePart = (uint32_t) ((heads & ~0xFFFFFFFFull) >> 32ull);

	*item = headsDeferFreePart;
	uint64_t const newHeads = markerIndex | headsFreePart;
	if (Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newHeads) != heads) {
		goto RedoF; // transaction fail redo inserting this index into deferred free list
	}

}
