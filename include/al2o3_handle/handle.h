// License Summary: MIT see LICENSE file
#pragma once

#include "al2o3_thread/atomic.h"

// A 32 bit handle can access 16.7 million objects and 256 generations per handle
typedef struct { uint32_t handle; } Handle_Handle32;
// a 64 bit handle can access ~2^12 objects with 16.7 million generation per handle
typedef struct { uint64_t handle; } Handle_Handle64;

#define Handle_MaxHandles32 0x00FFFFFFu
#define Handle_GenerationBitShift32 24u
#define Handle_GenerationType32 uint8_t
#define Handle_GenerationSize32 sizeof(Handle_GenerationType32)

#define Handle_MaxHandles64 0x000000FFFFFFFFFFull
#define Handle_GenerationBitShift64 40ull
#define Handle_GenerationType64 uint32_t
#define Handle_GenerationSize64 sizeof(Handle_GenerationType64)
#define Handle_GenerationFlagsAlloced64 (0x1 << 24u)
#define Handle_GenerationFlagsLeaked64 (0x2 << 24u)

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

typedef struct Handle_Manager64 {
	uint64_t elementSize;
	uint64_t maxBlocks;
	uint32_t handlesPerBlockMask;
	uint32_t handlesPerBlockShift;

	uint32_t neverReissueOldHandles : 1;

	// we sometimes want to decrement and other times we need to swap the lists atomically
	// this kind of dcas isn't supported on any HW we target
	// so instead we use the fact that are handles are 64 bit and we have 128 bit atomics
	// we always update both free and deferred atomically at the same time
	// if any release or allocs have occured the transaction will detect and reverse
	Thread_Atomic128_t freeListHeads;

	// each block includes the data and the generations store
	Thread_AtomicPtr_t *blocks;

	Thread_Atomic64_t totalHandlesAllocated;

} Handle_Manager64;

AL2O3_EXTERN_C Handle_Manager32 *Handle_Manager32Create(uint32_t elementSize,
																												uint32_t allocationBlockSize,
																												uint32_t maxBlocks,
																												bool neverReissueOldHandles);
AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32 *manager);
AL2O3_EXTERN_C Handle_Manager32 *Handle_Manager32Clone(Handle_Manager32 *src);

AL2O3_EXTERN_C Handle_Handle32 Handle_Manager32Alloc(Handle_Manager32 *manager);
AL2O3_EXTERN_C void Handle_Manager32Release(Handle_Manager32 *manager, Handle_Handle32 handle);

AL2O3_EXTERN_C Handle_Manager64 *Handle_Manager64Create(uint32_t elementSize,
																												uint32_t allocationBlockSize,
																												uint32_t maxBlocks,
																												bool neverReissueOldHandles);
AL2O3_EXTERN_C void Handle_Manager64Destroy(Handle_Manager64 *manager);
AL2O3_EXTERN_C Handle_Manager64 *Handle_Manager64Clone(Handle_Manager64 *src);

AL2O3_EXTERN_C Handle_Handle64 Handle_Manager64Alloc(Handle_Manager64 *manager);
AL2O3_EXTERN_C void Handle_Manager64Release(Handle_Manager64 *manager, Handle_Handle64 handle);

AL2O3_FORCE_INLINE bool Handle_Manager32IsValid(Handle_Manager32 *manager,
																								Handle_Handle32 handle) {
	if (handle.handle == 0) {
		return false;
	}
	uint32_t const handleGen = handle.handle >> Handle_GenerationBitShift32;
	uint32_t const actualIndex = (handle.handle & Handle_MaxHandles32);
	uint32_t const blockIndex = actualIndex >> manager->handlesPerBlockShift;
	uint32_t const index = actualIndex & manager->handlesPerBlockMask;

	// fetch the base memory block for this index
	uint8_t *base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[blockIndex]);
	ASSERT(base);
	// point to generation data for this index
	Handle_GenerationType32
			*gen = base + ((manager->handlesPerBlockMask + 1) * manager->elementSize) + (index * Handle_GenerationSize32);

	return (handleGen == *gen);
}

AL2O3_FORCE_INLINE void *Handle_Manager32HandleToPtr(Handle_Manager32 *manager,
																										 Handle_Handle32 handle) {
	if (handle.handle == 0) {
		return NULL;
	}
	if(!Handle_Manager32IsValid(manager, handle)) {
		LOGERROR("Handle being converted to pointer is not valid!");
		return NULL;
	}

	// fetch the base memory block for this index
	uint32_t const actualIndex = (handle.handle & Handle_MaxHandles32);
	uint32_t const blockIndex = actualIndex >> manager->handlesPerBlockShift;
	uint32_t const index = actualIndex & manager->handlesPerBlockMask;

	uint8_t const
			*const base = (uint8_t *) Thread_AtomicLoadPtrRelaxed(&manager->blocks[blockIndex]);
	ASSERT(base);
	return (void *) (base + (index * manager->elementSize));
}

#define HANDLE_MANAGER64_GETBASE_CONST(manager, blockIndex ) (uint8_t const * const ) Thread_AtomicLoadPtrRelaxed(&manager->blocks[blockIndex]); ASSERT(base)
#define HANDLE_MANAGER64_GETGEN_CONST(manager, base, index) (Handle_GenerationType64 const * const) (base + \
																						((manager->handlesPerBlockMask + 1) * manager->elementSize) + \
																						(index * Handle_GenerationSize64)); ASSERT(index < (manager->handlesPerBlockMask + 1))
#define HANDLE_MANAGER64_MAKEHANDLE(gen, actualIndex) { ((uint64_t)(*gen & 0x00FFFFFFu)) << Handle_GenerationBitShift64 | actualIndex }

AL2O3_FORCE_INLINE bool Handle_Manager64IsValid(Handle_Manager64 *manager,
																								Handle_Handle64 handle) {
	if (handle.handle == 0) {
		return false;
	}
	uint64_t const handleGen = handle.handle >> Handle_GenerationBitShift64;
	uint64_t const actualIndex = (handle.handle & Handle_MaxHandles64);
	uint64_t const blockIndex = actualIndex >> manager->handlesPerBlockShift;
	uint64_t const index = actualIndex & manager->handlesPerBlockMask;

	// fetch the base memory block for this index
	uint8_t const * const base = HANDLE_MANAGER64_GETBASE_CONST(manager, blockIndex);
	Handle_GenerationType64 const * const gen = HANDLE_MANAGER64_GETGEN_CONST(manager, base, index);

	return (handleGen == (*gen & 0x00FFFFFFu));
}

AL2O3_FORCE_INLINE void *Handle_Manager64HandleToPtr(Handle_Manager64 *manager,
																										 Handle_Handle64 handle) {
	if (handle.handle == 0) {
		return NULL;
	}
	if(!Handle_Manager64IsValid(manager, handle)) {
		LOGERROR("Handle being converted to pointer is not valid!");
		return NULL;
	}

	// fetch the base memory block for this index
	uint64_t const actualIndex = (handle.handle & Handle_MaxHandles64);
	uint64_t const blockIndex = actualIndex >> manager->handlesPerBlockShift;
	uint64_t const index = actualIndex & manager->handlesPerBlockMask;

	uint8_t const * const base = HANDLE_MANAGER64_GETBASE_CONST(manager, blockIndex);

	return (void *) (base + (index * manager->elementSize));
}

AL2O3_FORCE_INLINE Handle_Handle64 Handle_Manager64IndexToHandle(Handle_Manager64 *manager, uint64_t actualIndex) {
	uint64_t const blockIndex = actualIndex >> manager->handlesPerBlockShift;
	uint64_t const index = actualIndex & manager->handlesPerBlockMask;

	// fetch the base memory block for this index
	uint8_t const * const base = HANDLE_MANAGER64_GETBASE_CONST(manager, blockIndex);

	// point to generation data for this index
	Handle_GenerationType64 *gen = (Handle_GenerationType64 *) (base +
			((manager->handlesPerBlockMask + 1) * manager->elementSize) +
			(index * Handle_GenerationSize64));

	if(*gen & Handle_GenerationFlagsAlloced64) {
		Handle_Handle64 handle = HANDLE_MANAGER64_MAKEHANDLE(gen, actualIndex);
		return handle;
	} else {
		Handle_Handle64 invalid = {0};
		return invalid;
	}
}