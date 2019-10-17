// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_handle/fixed.h"

AL2O3_EXTERN_C Handle_FixedManager32* Handle_FixedManager32Create(uint32_t elementSize, uint32_t totalHandleCount) {
	ASSERT(elementSize >= sizeof(uint32_t));
	ASSERT(totalHandleCount <= Handle_MaxFixedHandles32);

	size_t const allocSize =
					sizeof(Handle_FixedManager32) +
					(totalHandleCount * elementSize) +
					(totalHandleCount * sizeof(uint8_t));

	Handle_FixedManager32 *manager = (Handle_FixedManager32 *) MEMORY_CALLOC(1, allocSize);
	if(!manager) {
		return NULL;
	}
	manager->elementSize = elementSize;
	manager->totalHandleCount = totalHandleCount;

	uint8_t* elementMem = (uint8_t *) (manager+1);

	// init free list for new block
	for (uint32_t i = 0u; i < totalHandleCount; ++i) {
		uint32_t const index = i;
		void* addr = elementMem + (index * manager->elementSize);
		*((uint32_t *)addr) = 0xFF000000u | (index + 1);
	}

	// index zero is born generation 1
	*(elementMem + (totalHandleCount * manager->elementSize)) = 1;

	// fix last index to point to the invalid marker
	*((uint32_t*)(elementMem + ((totalHandleCount - 1) * manager->elementSize))) = 0;

	// repoint heads to start of the free list with an empty deferred list
	Thread_AtomicStore64Relaxed(&manager->freeListHeads, 0xFF000000ull);

	return manager;
}

AL2O3_EXTERN_C void Handle_FixedManager32Destroy(Handle_FixedManager32* manager) {
	if (!manager) {
		return;
	}
	MEMORY_FREE(manager);
}


AL2O3_EXTERN_C Handle_FixedHandle32 Handle_FixedManager32Alloc(Handle_FixedManager32* manager) {
	uint32_t noFreeCount = 0;

RedoD0:;
	// heads has 2 linked list packed in a 64 bit location. Its our transaction
	// backout test as well
	uint64_t const heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
	uint32_t const headsFreePart = (uint32_t)(heads & 0xFFFFFFFFull);
	uint64_t const headsDeferFreePart = heads & ~0xFFFFFFFFull;

	// check to see if the free list is empty
	if (headsFreePart == Handle_InvalidFixedHandle32) {
		// we need to swap the deferred into the free list as free list is empty
		if (headsDeferFreePart == (uint64_t)Handle_InvalidFixedHandle32) {
			// the deferred list is empty, so we have no free handles
			// we've have got no free handles to give! BUT
			// another thread might be working on it, so we retry for a bit
			noFreeCount++;
			if (noFreeCount == 1000000) {
				LOGWARNING("Manager has run out of handles");
				return Handle_InvalidFixedHandle32; // fail
			}
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
	uint32_t const index = headsFreePart & 0x00FFFFFF; // clean up the marker
	uint32_t *item = (uint32_t *) (((uint8_t*)(manager+1)) + (index * manager->elementSize));
	uint64_t const newHeads = headsDeferFreePart | *item;

	if (Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newHeads) != heads) {
		goto RedoD0; // something changed reverse the transaction
	}

	// the item is now ours to abuse
	// clear it out ready for its new life
	memset(item, 0, manager->elementSize);

	// now make the handle and return it
	uint8_t *gen = ((uint8_t*)(manager+1)) + (manager->totalHandleCount * manager->elementSize) + index;
	return index | ((uint32_t) *gen) << 24u;
}

AL2O3_EXTERN_C void Handle_FixedManager32Release(Handle_FixedManager32* manager, Handle_FixedHandle32 handle) {
	ASSERT((handle & Handle_MaxFixedHandles32) < manager->totalHandleCount);
	ASSERT(Handle_FixedManager32IsValid(manager, handle));

	uint32_t const index = handle & 0x00FFFFFF; // clean out the current generation

	uint32_t *item = (uint32_t *) (((uint8_t*)(manager+1)) + (index * manager->elementSize));
	uint8_t *gen = ((uint8_t*)(manager+1)) + (manager->totalHandleCount * manager->elementSize) + index;

	// update the generation of this index
	// intentional 8 bit integer overflow
	*gen = *gen + 1;
	if (*gen == 0 && index == 0) {
		*gen = 1;
	}

	uint64_t markerIndex = ((uint64_t)index | 0xFF000000ull) << 32ull; // marker

RedoF:;
	// add it to the deferred list without changing the free list
	// repeat until we get a transaction okay response from CAS
	uint64_t const heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
	uint64_t const headsFreePart = heads & 0xFFFFFFFFull;
	uint32_t const headsDeferFreePart = (uint32_t)((heads & ~0xFFFFFFFFull)>>32ull);

	*item = headsDeferFreePart;
	uint64_t const newHeads = markerIndex | headsFreePart;
	if (Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newHeads) != heads) {
		goto RedoF; // transaction fail redo inserting this index into deferred free list
	}

}
