// License Summary: MIT see LICENSE file
#pragma once

#include "al2o3_thread/atomic.h"

// A 32 bit handle can access 16.7 million objects and 256 generations per handle
// Handle_InvalidDynamicHandle32 == 0 to help catch clear before alloc bugs
typedef struct { uint32_t handle; } Handle_Handle32;
#define Handle_MaxHandles32 0x00FFFFFF

typedef struct Handle_Manager32 {
	uint32_t elementSize;
	uint32_t maxBlocks;
	uint32_t handlesPerBlockMask;
	uint32_t handlesPerBlockShift;

	uint32_t neverReissueOldHandles : 1;

	// we sometimes want to decrement and other times we need to swap the lists atomically
	// this kind of dcas isn't supported on any HW we target
	// so instead we use the fact that are handles are 32 bit and we have 64 bit atomics
	// we always update both free and deferred atomically at the same time
	// if any release or allocs have occured the transaction will detect and reverse
	Thread_Atomic64_t freeListHeads;

	// each block includes the data and the generations store
	Thread_AtomicPtr_t *blocks;

	Thread_Atomic32_t totalHandlesAllocated;

} Handle_Manager32;

AL2O3_EXTERN_C Handle_Manager32 *Handle_Manager32Create(uint32_t elementSize,
																																			uint32_t allocationBlockSize,
																																			uint32_t maxBlocks,
																																			bool neverReissueOldHandles);

AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32 *manager);

AL2O3_EXTERN_C Handle_Handle32 Handle_Manager32Alloc(Handle_Manager32 *manager);
AL2O3_EXTERN_C void Handle_Manager32Release(Handle_Manager32 *manager, Handle_Handle32 handle);

AL2O3_FORCE_INLINE bool Handle_Manager32IsValid(Handle_Manager32 *manager,
																											 Handle_Handle32 handle) {
	if(handle.handle == 0) {
		return false;
	}
	uint32_t const handleGen = handle.handle >> 24;
	uint32_t index = (handle.handle & Handle_MaxHandles32);

	// fetch the base memory block for this index
	uint8_t *base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[index >> manager->handlesPerBlockShift]);
	ASSERT(base);
	index = index & manager->handlesPerBlockMask;
	// point to generation data for this index
	uint8_t *gen = base + ((manager->handlesPerBlockMask + 1) * manager->elementSize) + index;

	return (handleGen == *gen);
}

AL2O3_FORCE_INLINE void *Handle_Manager32HandleToPtr(Handle_Manager32 *manager,
																														Handle_Handle32 handle) {
	if(handle.handle == 0) {
		return NULL;
	}
	// fetch the base memory block for this index
	uint32_t const index = (handle.handle & Handle_MaxHandles32);
	uint8_t const
			*const base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[index >> manager->handlesPerBlockShift]);
	ASSERT(base);
	// check the generation
	uint8_t const *const gen =
			base + ((manager->handlesPerBlockMask + 1) * manager->elementSize) + (index & manager->handlesPerBlockMask);
	ASSERT((handle.handle >> 24) == *gen);
	if ((handle.handle >> 24) != *gen) {
		return NULL;
	}
	return (void *) (base + ((index & manager->handlesPerBlockMask) * manager->elementSize));
}
