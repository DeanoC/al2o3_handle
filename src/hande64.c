// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_handle/handle.h"

AL2O3_FORCE_INLINE bool IsPow2(uint32_t num) {
	return ((num & (num - 1)) == 0);
}

AL2O3_FORCE_INLINE uint32_t NextPow2(uint32_t num) {
	num -= 1;
	num |= num >> 16u;
	num |= num >> 8u;
	num |= num >> 4u;
	num |= num >> 2u;
	num |= num >> 1u;

	return num + 1;
}

// assumes power of 2
AL2O3_FORCE_INLINE uint32_t SlowLog2(uint32_t num) {
	if (num == 0) {
		return 0;
	}
	uint32_t count = 0;
	do {
		num >>= 1u;
		count++;
	} while ((num & 0x1u) == 0);
	return count;
}

// return true to retry the allocation, false means no hope
static bool AllocNewBlock64(Handle_Manager64 *manager) {
	if (Thread_AtomicLoad64Relaxed(&manager->totalHandlesAllocated) >= Handle_MaxHandles64) {
		LOGWARNING("Allocated all handles already!");
		return false;
	}
	// first thing we need to do is claim our new index range
	uint64_t baseIndex =
			Thread_AtomicFetchAdd64Relaxed(&manager->totalHandlesAllocated, (manager->handlesPerBlockMask + 1));

	if (baseIndex >= (manager->handlesPerBlockMask + 1) * manager->maxBlocks) {
		LOGWARNING("Trying to allocate more than %i blocks! Increase block size or max blocks", manager->maxBlocks);
		Thread_AtomicFetchAdd64Relaxed(&manager->totalHandlesAllocated, -(int64_t)(manager->handlesPerBlockMask + 1));
		return false;
	}

	ASSERT((baseIndex >> manager->handlesPerBlockShift) < manager->maxBlocks);

	size_t const blockSize = ((manager->handlesPerBlockMask + 1) * manager->elementSize) +
			((manager->handlesPerBlockMask + 1) * Handle_GenerationSize64);

	uint8_t *base = (uint8_t *) MEMORY_CALLOC(1, blockSize);
	if (!base) {
		LOGWARNING("Out of memory!");
		return false;
	}

	Thread_AtomicStorePtrRelaxed(manager->blocks + (baseIndex >> manager->handlesPerBlockShift), base);

	// init free list for new block
	for (uint32_t i = 0u; i < (manager->handlesPerBlockMask + 1); ++i) {
		uint64_t const index = baseIndex + i;
		uint64_t *addr = (uint64_t *) (base + (i * manager->elementSize));
		// add marker and point to next entry
		*addr = 0xFFFFFF0000000000ull | (index + 1);
	}

	// link the new block into the free list and attach existing free list to the
	// end of this block
Redo:;
	uint128_t const heads = Thread_AtomicLoad128Relaxed(&manager->freeListHeads);
	uint64_t const headsFreePart = platform_GetLower128(heads);
	uint128_t const headsDeferFreePart = platform_ClearLower128(heads);
	ASSERT(((platform_GetLower128(heads) & Handle_MaxHandles64) >> manager->handlesPerBlockShift) < manager->maxBlocks);

	// we chain to the next entry in the free list without disturbing the deferred list
	uint128_t const newHeads = platform_Or128(headsDeferFreePart, platform_Load128From64(baseIndex));
	// point last new handle to existing free list (it might not be invalid by now)
	*((uint64_t *) (base + (manager->handlesPerBlockMask * manager->elementSize))) = headsFreePart;

	if (platform_Compare128(Thread_AtomicCompareExchange128Relaxed(&manager->freeListHeads, heads, newHeads),heads)) {
		goto Redo; // something changed reverse the transaction
	}

	return true;
}

AL2O3_EXTERN_C Handle_Manager64 *Handle_Manager64Create(uint32_t elementSize,
																												uint32_t handlesPerBlock,
																												uint32_t maxBlocks,
																												bool neverReissueOldHandles) {
	ASSERT(elementSize >= sizeof(uint64_t));

	if (!IsPow2(handlesPerBlock)) {
		LOGWARNING("handlesPerBlock (%u) should be a power of 2, using %u", handlesPerBlock, NextPow2(handlesPerBlock));
		handlesPerBlock = NextPow2(handlesPerBlock);
	}

	// each block has space for the data, the generation and the index into blocks for the base pointer
	size_t const blockSize = (handlesPerBlock * elementSize) +
			(handlesPerBlock * Handle_GenerationSize64);

	// first block is attached directly to the header
	size_t const allocSize = sizeof(Handle_Manager64)
			+ blockSize +
			8 + // padding to ensure atomics are at least 8 byte aligned
			(maxBlocks * sizeof(Thread_AtomicPtr_t));

	Handle_Manager64 *manager = (Handle_Manager64 *) MEMORY_CALLOC(1, allocSize);
	if (!manager) {
		return NULL;
	}
	manager->elementSize = elementSize;
	manager->handlesPerBlockMask = handlesPerBlock - 1;
	manager->handlesPerBlockShift = SlowLog2(handlesPerBlock);
	manager->neverReissueOldHandles = neverReissueOldHandles;
	manager->maxBlocks = maxBlocks;

	uint8_t *base = (uint8_t *) (manager + 1);
	if (!base) {
		MEMORY_FREE(manager);
		return NULL;
	}
	// get to blocks space with 8 byte alignment guarenteed
	manager->blocks = (Thread_AtomicPtr_t*)(((uintptr_t)base + blockSize + 0x8ull) & ~0x7ull);
	Thread_AtomicStorePtrRelaxed(manager->blocks + 0, base);
	Thread_AtomicStore64Relaxed(&manager->totalHandlesAllocated, handlesPerBlock);

	// init free list for new block
	// both gen and block index are zero'ed via calloc
	for (uint32_t i = 0u; i < handlesPerBlock; ++i) {
		uint64_t const index = i;
		void *addr = base + (i * manager->elementSize);
		*((uint64_t *) addr) = 0xFFFFFF0000000000ull | (index + 1);
	}

	// index zero is born generation 1
	*(Handle_GenerationType64*)((base + (handlesPerBlock * manager->elementSize))) = 1;

	// fix last index to point to the invalid marker
	*((uint64_t *) (base + ((handlesPerBlock - 1) * manager->elementSize))) = 0;

	// repoint heads to start of the free list with an empty deferred list
	Thread_AtomicStore128Relaxed(&manager->freeListHeads, platform_Load128From64(0xFFFFFF0000000000ull));

	return manager;
}

AL2O3_EXTERN_C void Handle_Manager64Destroy(Handle_Manager64 *manager) {
	if (!manager) {
		return;
	}

	// 0th block is embedded
	for (uint32_t i = 1u; i < manager->maxBlocks; ++i) {
		void *ptr = Thread_AtomicLoadPtrRelaxed(&manager->blocks[i]);
		if (ptr) {
			MEMORY_FREE(ptr);
		}
	}

	MEMORY_FREE(manager);
}

AL2O3_EXTERN_C Handle_Handle64 Handle_Manager64Alloc(Handle_Manager64 *manager) {
	uint32_t noFreeCount = 0;
Redo:;
	// heads has 2 linked list packed in a 128 bit location. Its our transaction backout test as well
	uint128_t const heads = Thread_AtomicLoad128Relaxed(&manager->freeListHeads);
	uint64_t const headsFreePart = platform_GetLower128(heads); // discard high part
	uint128_t const headsDeferFreePart = platform_ClearLower128(heads);
	ASSERT(((platform_GetLower128(heads) & Handle_MaxHandles64) >> manager->handlesPerBlockShift) < manager->maxBlocks);

	// check to see if the free list is empty
	if (headsFreePart == 0) {
		// we need to swap the deferred into the free list as free list is empty
		if (platform_CompareToZero128(headsDeferFreePart)) {
			// the deferred list is empty, so we have no free handles
			// we now do the tricky part of allocating a new block in a lock free
			// way *gulp*
			bool retry = AllocNewBlock64(manager);
			if (retry == false || noFreeCount >= 1000) {
				LOGWARNING("Manager has run out of handles");
				Handle_Handle64 invalid = {0}; // fail
				return invalid;
			}
			// try again but mark we've tried, allow a few attempts then give up
			noFreeCount++;
			goto Redo;
		} else {
			uint128_t const newheads = platform_ShiftUpperToLower128(headsDeferFreePart);
			// we move the defer list into the free list position and mark the deferred as empty
			// we don't even have to loop here as a transaction reverse is the same thing
			Thread_AtomicCompareExchange128Relaxed(&manager->freeListHeads, heads, newheads);
			goto Redo; // retry now
		}
	}
	// we chain to the next entry in the free list without disturbing the deferred list
	uint64_t const actualIndex = headsFreePart & Handle_MaxHandles64;
	// fetch the base memory block for this index
	uint64_t const baseIndex = actualIndex >> manager->handlesPerBlockShift;
	ASSERT(baseIndex < manager->maxBlocks);
	uint8_t *const base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[baseIndex]);
	ASSERT(base != NULL);
	uint64_t index = actualIndex & manager->handlesPerBlockMask;
	uint64_t *const item = (uint64_t *) (base + (index * manager->elementSize));
	ASSERT((uint8_t *) item < (base + ((manager->handlesPerBlockMask + 1) * manager->elementSize)));

	uint128_t const newHeads = platform_Or128(headsDeferFreePart, platform_Load128From64(*item));

	if (platform_Compare128(Thread_AtomicCompareExchange128Relaxed(&manager->freeListHeads, heads, newHeads),heads)) {
		goto Redo; // something changed reverse the transaction
	}

	// the item is now ours to abuse
	// clear it out ready for its new life
	memset(item, 0x0, manager->elementSize);

	// now make the handle and return it
	// point to generation data for this index
	Handle_GenerationType64 *gen = (Handle_GenerationType64 *)(base +
			((manager->handlesPerBlockMask + 1) * manager->elementSize) +
			(index * Handle_GenerationSize64));

	Handle_Handle64 handle = {
			.handle = ((uint64_t) *gen) << Handle_GenerationBitShift64 | actualIndex
	};
	return handle;
}

AL2O3_EXTERN_C void Handle_Manager64Release(Handle_Manager64 *manager, Handle_Handle64 handle) {
	ASSERT((handle.handle & Handle_MaxHandles64) < Thread_AtomicLoad64Relaxed(&manager->totalHandlesAllocated));
	ASSERT(Handle_Manager64IsValid(manager, handle));

	uint64_t const actualIndex = handle.handle & Handle_MaxHandles64; // clean out the current generation
	uint64_t const blockIndex = actualIndex >> manager->handlesPerBlockShift;
	uint64_t const index = actualIndex & manager->handlesPerBlockMask;
	ASSERT(blockIndex < manager->maxBlocks);

	// fetch the base memory block for this index
	uint8_t *base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[blockIndex]);
	// point to generation data for this index
	Handle_GenerationType64 *gen = (Handle_GenerationType64*)(base +
			((manager->handlesPerBlockMask + 1) * manager->elementSize) +
			(index * Handle_GenerationSize64));

	uint64_t *item = (uint64_t *) (base + (index * manager->elementSize));

	// update the generation of this index
	*gen = *gen + 1;
	*gen = *gen & 0x00FFFFFFu;
	if (*gen == 0 && manager->neverReissueOldHandles) {
		// after generation wrap around simply lose the handle
		// never putting it back in the free list means it never gets reused
		// tho will get freed when the manager is

		// poison the data
		memset(item, 0xDC, manager->elementSize);
		return;
	}
	// handle 0 special case
	if (*gen == 0 && actualIndex == 0) {
		*gen = 1;
	}

	uint128_t indexInUpper = platform_LoadUpper128From64(handle.handle | 0xFFFFFF0000000000ull);

	RedoF:;
	// add it to the deferred list without changing the free list
	// repeat until we get a transaction okay response from CAS
	uint128_t const heads = Thread_AtomicLoad128Relaxed(&manager->freeListHeads);
	uint128_t const headsFreePart = platform_ClearUpper128(heads);
	uint64_t const headsDeferFreePart = platform_GetUpper128(heads);
	ASSERT(((platform_GetLower128(heads) & Handle_MaxHandles64) >> manager->handlesPerBlockShift) < manager->maxBlocks);

	*item = headsDeferFreePart;
	uint128_t const newHeads = platform_Or128(indexInUpper, headsFreePart);
	if (platform_Compare128(Thread_AtomicCompareExchange128Relaxed(&manager->freeListHeads, heads, newHeads), heads)) {
		goto RedoF; // transaction fail redo inserting this index into deferred free list
	}

}
