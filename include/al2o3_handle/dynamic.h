// License Summary: MIT see LICENSE file
#pragma once

// A 32 bit handle can access 16.7 million objects and 256 generations per handle
// Handle_InvalidDynamicHandle32 == 0 to help catch clear before alloc bugs
typedef uint32_t Handle_DynamicHandle32;
#define Handle_MaxDynamicHandles32 0x00FFFFFF
// Handle_InvalidFixedHandle32 == 0 to help catch clear before alloc bugs
#define Handle_InvalidDynamicHandle32 0

#define Handle_DynamicManagerMaxBlocks 256

typedef struct Handle_DynamicManager32 {
	uint32_t elementSize;
	uint32_t handlesPerBlockMask;
	uint32_t handlesPerBlockShift;


	// we sometimes want to decrement and other times we need to swap the lists atomically
	// this kind of dcas isn't supported on any HW we target
	// so instead we use the fact that are handles are 32 bit and we have 64 bit atomics
	// we always update both free and deferred atomically at the same time
	// if any release or allocs have occured the transaction will detect and reverse
	Thread_Atomic64_t freeListHeads;

	// each block includes the data and the generations store
	Thread_AtomicPtr_t blocks[Handle_DynamicManagerMaxBlocks];

	Thread_Atomic32_t totalHandlesAllocated;

} Handle_DynamicManager32;


AL2O3_EXTERN_C Handle_DynamicManager32* Handle_DynamicManager32Create(uint32_t elementSize, uint32_t allocationBlockSize);

AL2O3_EXTERN_C void Handle_DynamicManager32Destroy(Handle_DynamicManager32* manager);

AL2O3_EXTERN_C Handle_DynamicHandle32 Handle_DynamicManager32Alloc(Handle_DynamicManager32* manager);
AL2O3_EXTERN_C void Handle_DynamicManager32Release(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle);

AL2O3_EXTERN_C bool Handle_DynamicManager32IsValid(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle);

// this returns the data ptr for the handle but is unsafe without manual locks for dynamic managers.
AL2O3_EXTERN_C void* Handle_DynamicManager32HandleToPtr(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle);
