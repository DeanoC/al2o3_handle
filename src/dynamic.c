// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/thread.h"
#include "al2o3_handle/dynamic.h"
#include "al2o3_thread/atomic.h"

#define MaxHandles32Bit 0x00FFFFFFu
#define MUTEX_LOCK(x) if(x->mutexPtr) { Thread_MutexAcquire(x->mutexPtr); }
#define MUTEX_UNLOCK(x) if(x->mutexPtr) { Thread_MutexRelease(x->mutexPtr); }

#define DEFAULT_DEFER_FLUSH 2
#define DEFAULT_DELAYED_FLUSH 100
#define VECTOR_GROWTH_RATE 1.00

#define FREELIST_PART(x) ((uint32_t)((x&0xFFFFFFFFull)))
#define DFREELIST_PART(x) ((uint32_t)((x&~0xFFFFFFFFull)>>32ull))

typedef struct Handle_DynamicManager32 {
	size_t elementSize;
	void *baseHandleAddress;
	uint8_t *baseHandleGen;

	Thread_Atomic64_t freeListHeads;

	Thread_Atomic32_t handleAllocatedCount;

	// fixed managers don't have this portion following!
	Thread_Atomic32_t numHandlesInBlock;
	Thread_Mutex *mutexPtr;

	uint32_t deferThreshold;
	uint32_t delayedThreshold;

	Thread_Atomic32_t blockAllocatedSinceDeferredFlush;

	Thread_Atomic32_t delayedFreeListHead;
	Thread_Atomic32_t deferredFlushSinceDelayedFlush;

	// for internally allocated mutex, it follows here
} Handle_DynamicManager32;

static AL2O3_FORCE_INLINE uint64_t SwapFreeLists(uint64_t heads) {
uint64_t const freeVal = FREELIST_PART(heads);
uint64_t const defVal = DFREELIST_PART(heads);
uint64_t const newHeads = freeVal << 32ull | defVal; // swap lists
return newHeads;
}

static AL2O3_FORCE_INLINE bool CheckGeneration32(Handle_DynamicManager32* manager, uint32_t handle) {
uint32_t const handleGen = handle >> 24;
uint32_t const index = (handle & MaxHandles32Bit);
uint8_t *gen = manager->baseHandleGen + index;
return (handleGen == *gen);
}

AL2O3_FORCE_INLINE void *ElementAddress32(Handle_DynamicManager32* manager, uint32_t handle) {
	ASSERT((handle & MaxHandles32Bit) < Thread_AtomicLoad32Relaxed(&manager->handleAllocatedCount));

	uint32_t const index = (handle & MaxHandles32Bit);
	return ((uint8_t *) manager->baseHandleAddress) + (index * manager->elementSize);
}

// not thread safe, needs external synchronisation!
static void Handle_ManagerBaseNewHandleBlock32(Handle_DynamicManager32 *manager, uint32_t numHandlesInBlock) {
	ASSERT(manager);

	// under a mutex or at initalisation so this is safe
	uint64_t heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);

	ASSERT(FREELIST_PART(heads) == Handle_InvalidDynamicHandle32);

	uint32_t oldHandleCount = Thread_AtomicLoad32Relaxed(&manager->handleAllocatedCount);
	uint32_t newHandleCount = oldHandleCount + numHandlesInBlock;
	ASSERT(newHandleCount <= MaxHandles32Bit);

	Thread_AtomicStore32Relaxed(&manager->handleAllocatedCount, newHandleCount);

	manager->baseHandleAddress = MEMORY_REALLOC(manager->baseHandleAddress, newHandleCount * manager->elementSize);
	manager->baseHandleGen = (uint8_t *) MEMORY_REALLOC(manager->baseHandleGen, newHandleCount * sizeof(uint8_t));

	// init free list for new block
	for (uint32_t i = 0u; i < numHandlesInBlock; ++i) {
		uint32_t const index = oldHandleCount + i;
		*((uint32_t *) ElementAddress32(manager, index)) = 0xFF000000u | index + 1;
		manager->baseHandleGen[index] = 0;
	}
	// index zero is born generation 1
	if (oldHandleCount == 0) {
		manager->baseHandleGen[0] = 1;
	}

	// fix last index to point to the deferred list as well
	uint32_t *item = (uint32_t *) ElementAddress32(manager, newHandleCount - 1);
	*item = DFREELIST_PART(heads);

	// repoint heads to start of the new block with an empty deferred list
	uint64_t const newHeads = 0xFF000000u | oldHandleCount | (0ull << 32ull);
	Thread_AtomicStore64Relaxed(&manager->freeListHeads, newHeads);
}

// not thread safe, needs external synchronisation!
static void Handle_ManagerNewHandleBlock32(Handle_DynamicManager32 *manager, uint32_t numHandlesInBlock) {
	ASSERT(manager);
	uint64_t heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
	if(FREELIST_PART(heads) != Handle_InvalidDynamicHandle32) {
		// back off as another thread has done the job for us
		return;
	}

	// we've built a bunch of deferred handles, so lets use them before allocating
	// this only works due to the mutex its not atomic!

	// every time we add a new block of handles we also recycle the unused handles.
	// this means there is at least NumHandlesInBlock allocations before a handle
	// is reused. Which means NumHandlesInBlock * 256 allocation of the same handle
	// can occur before a potential invalid handle generation would be missed.
	// this does cause over allocations as new blocks are added when there are free
	// handles to use BUT we after a number of block allocs have has built up we try
	// just using the deferred list without allocing

	if (DFREELIST_PART(heads) != Handle_InvalidDynamicHandle32 &&
			Thread_AtomicLoad32Relaxed(&manager->blockAllocatedSinceDeferredFlush) >= manager->deferThreshold) {
		Thread_AtomicStore64Relaxed(&manager->freeListHeads, SwapFreeLists(heads));

		Thread_AtomicStore32Relaxed(&manager->blockAllocatedSinceDeferredFlush, 0);
		if (Thread_AtomicLoad32Relaxed(&manager->delayedFreeListHead) != Handle_InvalidDynamicHandle32) {
			Thread_AtomicFetchAdd32Relaxed(&manager->deferredFlushSinceDelayedFlush, 1);
		}

		return;
	}

	if (Thread_AtomicLoad32Relaxed(&manager->delayedFreeListHead) != Handle_InvalidDynamicHandle32 &&
			Thread_AtomicLoad32Relaxed(&manager->deferredFlushSinceDelayedFlush) >= manager->delayedThreshold) {
		// see if we can just use the delayed list avoiding a new allocation
		uint32_t oldValue = 0;
		RedoD1:
		oldValue = Thread_AtomicLoad32Relaxed(&manager->delayedFreeListHead);
		if (Thread_AtomicCompareExchange32Relaxed(&manager->delayedFreeListHead, oldValue, Handle_InvalidDynamicHandle32)
				!= oldValue) {
			goto RedoD1;
		}
		uint64_t const defVal = DFREELIST_PART(heads);
		uint64_t const newHeads = defVal << 32ull | oldValue;

		Thread_AtomicStore64Relaxed(&manager->freeListHeads, newHeads);
		Thread_AtomicStore32Relaxed(&manager->deferredFlushSinceDelayedFlush, 0);
		return;
	}

	Handle_ManagerBaseNewHandleBlock32((Handle_DynamicManager32 *) manager, numHandlesInBlock);

	Thread_AtomicFetchAdd32Relaxed(&manager->blockAllocatedSinceDeferredFlush, 1);
}

AL2O3_EXTERN_C Handle_DynamicManager32* Handle_ManagerDynamic32CreateWithMutex(size_t elementSize,
																																							 size_t startingSize,
																																							 size_t allocationBlockSize,
																																							 Thread_Mutex *mutex) {
ASSERT(elementSize >= sizeof(uint32_t));
Handle_DynamicManager32 *manager = (Handle_DynamicManager32 *) MEMORY_CALLOC(1, sizeof(Handle_DynamicManager32));
manager->mutexPtr = mutex;

MUTEX_LOCK(manager)

Thread_AtomicStore32Relaxed(&manager->numHandlesInBlock, (uint32_t) allocationBlockSize);
manager->elementSize = elementSize;
manager->deferThreshold = DEFAULT_DEFER_FLUSH;
manager->delayedThreshold = DEFAULT_DELAYED_FLUSH;
Thread_AtomicStore64Relaxed(&manager->freeListHeads, 0); // this is actually 2 invalids
Thread_AtomicStore32Relaxed(&manager->delayedFreeListHead, Handle_InvalidDynamicHandle32);

Handle_ManagerNewHandleBlock32(manager, (uint32_t)startingSize);

// make index 0 start at gen 0, means a 0 handle is always invalid
manager->baseHandleGen[0] = 1;

MUTEX_UNLOCK(manager)

return manager;

}

AL2O3_EXTERN_C Handle_DynamicManager32* Handle_ManagerDynamic32Create(size_t elementSize,
																																			size_t startingSize,
																																			size_t allocationBlockSize) {
ASSERT(elementSize >= sizeof(uint32_t));
Handle_DynamicManager32
		*manager = (Handle_DynamicManager32 *) MEMORY_CALLOC(1, sizeof(Handle_DynamicManager32) + sizeof(Thread_Mutex));
manager->mutexPtr = (Thread_Mutex *) (manager + 1);
Thread_MutexCreate(manager->mutexPtr);

MUTEX_LOCK(manager)

Thread_AtomicStore32Relaxed(&manager->numHandlesInBlock, (uint32_t) allocationBlockSize);
manager->elementSize = elementSize;
manager->deferThreshold = DEFAULT_DEFER_FLUSH;
manager->delayedThreshold = DEFAULT_DELAYED_FLUSH;

Thread_AtomicStore64Relaxed(&manager->freeListHeads, 0); // this is actually 2 invalids
Thread_AtomicStore32Relaxed(&manager->delayedFreeListHead, Handle_InvalidDynamicHandle32);

Handle_ManagerNewHandleBlock32(manager, (uint32_t) startingSize);

// make index 0 start at gen 0, means a 0 handle is always invalid
manager->baseHandleGen[0] = 1;

MUTEX_UNLOCK(manager)

return manager;
}

AL2O3_EXTERN_C void Handle_DynamicManager32Destroy(Handle_DynamicManager32* manager) {
	if (!manager) {
		return;
	}
	MEMORY_FREE(manager->baseHandleAddress);
	MEMORY_FREE(manager->baseHandleGen);

	Handle_DynamicManager32 *dyna = (Handle_DynamicManager32 *) manager;
	if (dyna->mutexPtr == (Thread_Mutex *) (dyna + 1)) {
		Thread_MutexDestroy(dyna->mutexPtr);
	}

	MEMORY_FREE(manager);
}

// lock free except non fixed manager needs to allocate more handles
AL2O3_EXTERN_C Handle_DynamicHandle32 Handle_DynamicManager32Alloc(Handle_DynamicManager32* manager) {
// we sometimes want to decrement and other times we need to swap the lists atomically
// this kind of dcas isn't supported on any HW

// so instead we use the fact that are handles are 32 bit and are atomics are 64 bit
// we always update both free and deferred atomically at the same time

// if any release or allocs have occured the atomic compare will fail
// and we restart the operation

uint32_t noFreeCount = 0;
RedoD0:;
// delink the deferred free list atomically
uint64_t const heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
uint64_t newHeads;
// check to see if the free list is empty
if (FREELIST_PART(heads) == Handle_InvalidDynamicHandle32) {
Handle_DynamicManager32 *dyna = (Handle_DynamicManager32 *) manager;
MUTEX_LOCK(dyna)
uint32_t numHandlesInBlock = Thread_AtomicLoad32Relaxed(&dyna->numHandlesInBlock);
Handle_ManagerNewHandleBlock32(dyna, numHandlesInBlock);

numHandlesInBlock = (uint32_t) (((float) numHandlesInBlock * VECTOR_GROWTH_RATE) + 0.5f);
Thread_AtomicStore32Relaxed(&dyna->numHandlesInBlock, numHandlesInBlock);
MUTEX_UNLOCK(dyna)
goto RedoD0; // retry now
} else {
// in this case we look up the next entry in the list without
// changeing the deferred list
uint32_t index = (uint32_t) FREELIST_PART(heads);
// this is annoying!
MUTEX_LOCK(manager)
uint32_t *item = (uint32_t *) ElementAddress32(manager, index);
MUTEX_UNLOCK(manager)

newHeads = ((uint64_t) DFREELIST_PART(heads)) << 32ull | *item;

if (Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newHeads) != heads) {
goto RedoD0; // something changed reverse the transaction
}
memset(item, 0, manager->elementSize);

// now make the handle
index = index & MaxHandles32Bit;
uint8_t *gen = manager->baseHandleGen + index;
return index | ((uint32_t) *gen) << 24u;
}
}

AL2O3_EXTERN_C void Handle_DynamicManager32Release(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle) {
	ASSERT((handle & MaxHandles32Bit) < Thread_AtomicLoad32Relaxed(&manager->handleAllocatedCount));

	uint32_t index = (handle & MaxHandles32Bit);

	uint8_t *gen;
	MUTEX_LOCK(manager)
	ASSERT(CheckGeneration32(manager, handle));
	gen = manager->baseHandleGen + index;
	// update the generation of this index
	// intentional 8 bit integer overflow
	*gen = *gen + 1;
	if (*gen == 0 && index == 0) {
		*gen = 1;
	}
	if (*gen == 0) {
		Handle_DynamicManager32 *dyna = (Handle_DynamicManager32 *) manager;
		Redo1:;
		// add to the delayed reuse list
		uint32_t *item = (uint32_t *) ElementAddress32(manager, index);
		*item = Thread_AtomicLoad32Relaxed(&dyna->delayedFreeListHead);
		uint32_t const oldValue = *item;
		if (Thread_AtomicCompareExchange32Relaxed(&dyna->delayedFreeListHead, oldValue, index) != oldValue) {
			goto Redo1;
		}
		MUTEX_UNLOCK(dyna)
		return;
	}
	uint32_t markerIndex = index | 0xFF000000; // marker
	Redo:;
	// add it to the deferred list without changing the free list
	uint32_t* item = (uint32_t *) ElementAddress32(manager, index);
	uint64_t const heads = Thread_AtomicLoad64Relaxed(&manager->freeListHeads);
	*item = DFREELIST_PART(heads);
	uint64_t flp = FREELIST_PART(heads);
	uint64_t const newHeads = ((uint64_t) markerIndex) << 32ull | flp;

	if (Thread_AtomicCompareExchange64Relaxed(&manager->freeListHeads, heads, newHeads) != heads) {
		goto Redo;
	}

	MUTEX_UNLOCK(manager)
}

AL2O3_EXTERN_C bool Handle_DynamicManager32IsValid(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle) {
return CheckGeneration32(manager, handle);
}

AL2O3_EXTERN_C void *Handle_DynamicManager32HandleToPtr(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle) {
	ASSERT(CheckGeneration32(manager, handle));
	return ElementAddress32(manager, handle);
}

AL2O3_EXTERN_C void Handle_DynamicManager32Lock(Handle_DynamicManager32* manager) {
	MUTEX_LOCK(manager)
}

AL2O3_EXTERN_C void Handle_DynamicManager32Unlock(Handle_DynamicManager32* manager) {
	MUTEX_UNLOCK(manager)
}


AL2O3_EXTERN_C void Handle_DynamicManager32SetDeferredFlushThreshold(Handle_DynamicManager32* manager, uint32_t threshold) {
	Handle_DynamicManager32 *dyna = (Handle_DynamicManager32 *) manager;
	dyna->deferThreshold = threshold;
}

AL2O3_EXTERN_C void Handle_DynamicManager32SetDelayedFlushThreshold(Handle_DynamicManager32* manager, uint32_t threshold) {
	Handle_DynamicManager32 *dyna = (Handle_DynamicManager32 *) manager;
	dyna->delayedThreshold = threshold;
}

AL2O3_EXTERN_C uint32_t Handle_DynamicManager32HandleAllocatedCount(Handle_DynamicManager32* manager) {
return Thread_AtomicLoad32Relaxed(&manager->handleAllocatedCount);
}

AL2O3_EXTERN_C void Handle_DynamicManager32CopyToMemory(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle, void *dst) {

	MUTEX_LOCK(manager)
	ASSERT(CheckGeneration32(manager, handle));

	memcpy(dst, ElementAddress32(manager, handle), manager->elementSize);

	MUTEX_UNLOCK(manager)
}

AL2O3_EXTERN_C void Handle_DynamicManager32CopyFromMemory(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle, void const *src) {

	MUTEX_LOCK(manager)

	ASSERT(CheckGeneration32(manager, handle));

	memcpy(ElementAddress32(manager, handle), src, manager->elementSize);

	MUTEX_UNLOCK(manager)
}

