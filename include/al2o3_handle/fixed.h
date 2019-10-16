// License Summary: MIT see LICENSE file
#pragma once

// A 32 bit handle can access 16.7 million objects and 256 generations per handle
// Handle_InvalidDynamicHandle32 == 0 to help catch clear before alloc bugs
typedef uint32_t Handle_FixedHandle32;
#define Handle_MaxFixedHandles32 0x00FFFFFF
// Handle_InvalidFixedHandle32 == 0 to help catch clear before alloc bugs
#define Handle_InvalidFixedHandle32 0

typedef struct Handle_FixedManager32 {
	uint32_t elementSize;
	uint32_t totalHandleCount;

	// we sometimes want to decrement and other times we need to swap the lists atomically
	// this kind of dcas isn't supported on any HW we target
	// so instead we use the fact that are handles are 32 bit and we have 64 bit atomics
	// we always update both free and deferred atomically at the same time
	// if any release or allocs have occured the transaction will detect and reverse
	Thread_Atomic64_t freeListHeads;

} Handle_FixedManager32;

AL2O3_EXTERN_C Handle_FixedManager32* Handle_FixedManager32Create(uint32_t elementSize, uint32_t totalHandleCount);
AL2O3_EXTERN_C void Handle_FixedManager32Destroy(Handle_FixedManager32* manager);

AL2O3_EXTERN_C Handle_FixedHandle32 Handle_FixedManager32Alloc(Handle_FixedManager32* manager);
AL2O3_EXTERN_C void Handle_FixedManager32Release(Handle_FixedManager32* manager, Handle_FixedHandle32 handle);

AL2O3_FORCE_INLINE bool Handle_FixedManager32IsValid(Handle_FixedManager32* manager, Handle_FixedHandle32 handle) {
	uint32_t const handleGen = handle >> 24;
	uint32_t const index = (handle & Handle_MaxFixedHandles32);
	uint8_t *gen = ((uint8_t*)(manager+1)) + (manager->totalHandleCount * manager->elementSize) + index;
	return (handleGen == *gen);
}

AL2O3_FORCE_INLINE void* Handle_FixedManager32HandleToPtr(Handle_FixedManager32* manager, Handle_FixedHandle32 handle) {
	if(!Handle_FixedManager32IsValid(manager, handle)) {
		return NULL;
	}

	uint32_t const index = (handle & Handle_MaxFixedHandles32);
	return ((uint8_t *)(manager+1)) + (index * manager->elementSize);
}


