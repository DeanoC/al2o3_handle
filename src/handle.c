#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/thread.h"
#include "al2o3_handle/handle.h"
#include "al2o3_handle/handlemanager.h"
#include "al2o3_thread/atomic.h"

#define MaxHandles32Bit 0x00FFFFFFu
#define MUTEX_LOCK if(manager->mutexPtr) { Thread_MutexAcquire(manager->mutexPtr); }
#define MUTEX_UNLOCK if(manager->mutexPtr) { Thread_MutexRelease(manager->mutexPtr); }


typedef struct Handle_Manager32 {
	Thread_Mutex inbuiltMutex;
	Thread_Mutex* mutexPtr;

	uint32_t numHandlesInBlock;
	size_t elementSize;

	void* baseHandleAddress;
	uint8_t* baseHandleGen;

	Thread_Atomic32_t freeListHead;

	bool fixed;

	uint32_t handleAllocatedCount;
	Thread_Atomic32_t deferredFreeListHead;
	Thread_Atomic32_t blockAllocatedSinceDeferredFlush;

} Handle_Manager32;

AL2O3_FORCE_INLINE bool CheckGeneration32(Handle_Manager32Handle manager, uint32_t handle) {
	uint32_t const handleGen = handle >> 24;
	uint32_t const index = (handle & MaxHandles32Bit);
	uint8_t* gen = manager->baseHandleGen + index;
	return (handleGen == *gen);
}

AL2O3_FORCE_INLINE void* ElementAddress32(Handle_Manager32Handle manager, uint32_t index) {
	return ((uint8_t*) manager->baseHandleAddress) + (index * manager->elementSize);
}

// not thread safe, needs external synchronisation!
static void Handle_ManagerNewHandleBlock32(Handle_Manager32Handle manager) {
	ASSERT(manager);

	// we've built a bunch of deferred handles, so lets use them before allocating
	// this only works due to the mutex its not atomic!
	uint32_t index = Thread_AtomicLoad32Relaxed(&manager->freeListHead);
	ASSERT(index == ~0);

	// every time we add a new block of handles we also recycle the unused handles.
	// this means there is at least NumHandlesInBlock allocations before a handle
	// is reused. Which means NumHandlesInBlock * 256 allocation of the same handle
	// can occur before a potential invalid handle generation would be missed.
	// this does cause over allocations as new blocks are added when there are free
	// handles to use BUT we after a number of block allocs have has built up we try
	// just using the deferred list without allocing

	if( Thread_AtomicLoad32Relaxed(&manager->blockAllocatedSinceDeferredFlush) >= 4) {
		// see if we can just use the deferred list avoiding a new allocation
		uint32_t oldValue = 0;
RedoD:
		oldValue = Thread_AtomicLoad32Relaxed(&manager->deferredFreeListHead);
		if( Thread_AtomicCompareExchange32Relaxed(&manager->deferredFreeListHead, oldValue, ~0u) != oldValue) {
			goto RedoD;
		}

		Thread_AtomicStore32Relaxed(&manager->freeListHead, oldValue);
		Thread_AtomicStore32Relaxed(&manager->blockAllocatedSinceDeferredFlush, 0);
		return;
	}

	size_t newHandleCount = manager->handleAllocatedCount + manager->numHandlesInBlock;
	manager->baseHandleAddress = MEMORY_REALLOC(manager->baseHandleAddress, newHandleCount * manager->elementSize);
	manager->baseHandleGen = (uint8_t*) MEMORY_REALLOC(manager->baseHandleGen, newHandleCount * sizeof(uint8_t));

	// init free list for new block
	for (uint32_t i = 0u; i < manager->numHandlesInBlock; ++i) {
		uint32_t const index = manager->handleAllocatedCount + i;
		*((uint32_t*)ElementAddress32(manager, index)) = index + 1;
		manager->baseHandleGen[index] = 0;
	}
	// fix last index to point to the deferred list as well
	uint32_t* item = (uint32_t*)ElementAddress32(manager, manager->handleAllocatedCount + manager->numHandlesInBlock -1);
Redo:
	*item = Thread_AtomicLoad32Relaxed(&manager->deferredFreeListHead);
	uint32_t const oldValue = *item;
	if( Thread_AtomicCompareExchange32Relaxed(&manager->deferredFreeListHead, oldValue, index) != oldValue) {
		goto Redo;
	}

	// repoint head to start of the new block
	Thread_AtomicStore32Relaxed(&manager->freeListHead, manager->handleAllocatedCount);
	Thread_AtomicFetchAdd32Relaxed(&manager->blockAllocatedSinceDeferredFlush, 1);

	manager->handleAllocatedCount = (uint32_t)newHandleCount;
}

AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32CreateWithMutex(size_t elementSize, size_t allocationBlockSize, Thread_Mutex* mutex) {
	ASSERT(elementSize >= sizeof(uint32_t));
	Handle_Manager32Handle manager = (Handle_Manager32Handle) MEMORY_CALLOC(1, sizeof(Handle_Manager32));
	manager->mutexPtr = mutex;

	MUTEX_LOCK

	manager->numHandlesInBlock = (uint32_t) allocationBlockSize;
	manager->elementSize = elementSize;
	Thread_AtomicStore32Relaxed(&manager->freeListHead, ~0u);
	Thread_AtomicStore32Relaxed(&manager->deferredFreeListHead, ~0u);

	Handle_ManagerNewHandleBlock32(manager);

	MUTEX_UNLOCK

	return manager;

}

AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32CreateNoLocks(size_t elementSize, size_t allocationBlockSize) {
	return Handle_Manager32CreateWithMutex(elementSize, allocationBlockSize, NULL);
}

AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32Create(size_t elementSize, size_t allocationBlockSize) {
	ASSERT(elementSize >= sizeof(uint32_t));
	Handle_Manager32Handle manager = (Handle_Manager32Handle) MEMORY_CALLOC(1, sizeof(Handle_Manager32));
	Thread_MutexCreate(&manager->inbuiltMutex);
	manager->mutexPtr = &manager->inbuiltMutex;

	MUTEX_LOCK

	manager->numHandlesInBlock = (uint32_t) allocationBlockSize;
	manager->elementSize = elementSize;
	Thread_AtomicStore32Relaxed(&manager->freeListHead, ~0u);
	Thread_AtomicStore32Relaxed(&manager->deferredFreeListHead, ~0u);

	Handle_ManagerNewHandleBlock32(manager);

	MUTEX_UNLOCK

	return manager;
}

AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32FixedSize(size_t elementSize, size_t totalHandleCount) {
	Handle_Manager32Handle manager = Handle_Manager32CreateWithMutex(elementSize, totalHandleCount, NULL);
	manager->fixed = true;
	return manager;
}

AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32Handle manager) {
	if (!manager) {
		return;
	}
	MEMORY_FREE(manager->baseHandleAddress);
	MEMORY_FREE(manager->baseHandleGen);

	if (manager->mutexPtr == &manager->inbuiltMutex) {
		Thread_MutexDestroy(&manager->inbuiltMutex);
	}

	MEMORY_FREE(manager);
}


AL2O3_EXTERN_C Handle_Handle32 Handle_Manager32Alloc(Handle_Manager32Handle manager) {

	uint32_t index = 0;

RedoAllocation:
	// first we check if the some free on the free list
	index = Thread_AtomicLoad32Relaxed(&manager->freeListHead);
	if(index != ~0u) {
		uint32_t* item = (uint32_t*)ElementAddress32(manager, index);
		if( Thread_AtomicCompareExchange32Relaxed(&manager->freeListHead, index, *item) != index) {
			goto RedoAllocation;
		}
		// pop head and return it
		ASSERT((index & ~MaxHandles32Bit) == 0);

		memset(item, 0, manager->elementSize);

		// now make the handle
		uint8_t* gen = manager->baseHandleGen + index;

		return index | ((uint32_t)*gen) << 24u;
	} else {
		if(manager->fixed) {
			return (Handle_Handle32)0xFFFFFFFFFu;
		}

		MUTEX_LOCK
		// allocate a new block and then redo
		Handle_ManagerNewHandleBlock32(manager);
		MUTEX_UNLOCK

		goto RedoAllocation;
	}

}

// TODO possibly release only needs to take a mutex versus allocation if atomic
// are used or at least a smaller mutex against other releases and locks.
AL2O3_EXTERN_C void Handle_Manager32Release(Handle_Manager32Handle manager, Handle_Handle32 handle) {
	ASSERT((handle & MaxHandles32Bit)  < manager->handleAllocatedCount );
	ASSERT( CheckGeneration32(manager, handle));

	uint32_t index = (handle & MaxHandles32Bit);
	uint32_t* item = (uint32_t*)ElementAddress32(manager, index);
	// update the generation of this index
	uint8_t* gen = manager->baseHandleGen + index;
	*gen = *gen + 1;

	if(manager->fixed) {
		// add it to the free list immediately for fixed sized handle manager
RedoF:
		*item = Thread_AtomicLoad32Relaxed(&manager->freeListHead);
		uint32_t const oldValue = *item;
		if (Thread_AtomicCompareExchange32Relaxed(&manager->freeListHead, oldValue, index) != oldValue) {
			goto RedoF;
		}
	} else {
		// add it to the deferred list
Redo:
		*item = Thread_AtomicLoad32Relaxed(&manager->deferredFreeListHead);
		uint32_t const oldValue = *item;
		if (Thread_AtomicCompareExchange32Relaxed(&manager->deferredFreeListHead, oldValue, index) != oldValue) {
			goto Redo;
		}
	}
}

AL2O3_EXTERN_C bool Handle_Manager32IsValid(Handle_Manager32Handle manager, Handle_Handle32 handle) {
	MUTEX_LOCK

	bool valid = CheckGeneration32(manager, handle);

	MUTEX_UNLOCK
	return valid;
}

AL2O3_EXTERN_C void* Handle_Manager32ToPtrUnsafe(Handle_Manager32Handle manager, Handle_Handle32 handle) {
	ASSERT(CheckGeneration32(manager, handle));
	return ElementAddress32(manager, handle);
}

AL2O3_EXTERN_C void* Handle_Manager32ToPtrLock(Handle_Manager32Handle manager, Handle_Handle32 handle){
	MUTEX_LOCK

	ASSERT(CheckGeneration32(manager, handle));

	return ElementAddress32(manager, handle);
}

AL2O3_EXTERN_C void Handle_Manager32ToPtrUnlock(Handle_Manager32Handle manager) {
	MUTEX_UNLOCK
}

AL2O3_EXTERN_C void Handle_Manager32CopyTo(Handle_Manager32Handle manager, Handle_Handle32 handle, void* dst) {
	MUTEX_LOCK
	ASSERT(CheckGeneration32(manager, handle));

	memcpy(dst, ElementAddress32(manager, handle), manager->elementSize);

	MUTEX_UNLOCK
}

AL2O3_EXTERN_C void Handle_Manager32CopyFrom(Handle_Manager32Handle manager, Handle_Handle32 handle, void const* src) {
	MUTEX_LOCK

	ASSERT(CheckGeneration32(manager, handle));

	memcpy(ElementAddress32(manager, handle), src, manager->elementSize);

	MUTEX_UNLOCK
}
