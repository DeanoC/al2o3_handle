// License Summary: MIT see LICENSE file
#pragma once

#include "al2o3_thread/thread.h"
typedef uint32_t Handle_DynamicHandle32;
typedef struct Handle_DynamicManager32 Handle_DynamicManager32;

// Handle_InvalidDynamicHandle32 == 0 to help catch clear before alloc bugs
#define Handle_InvalidDynamicHandle32 0

// dynamic handle manager with internal mutex used when required
AL2O3_EXTERN_C Handle_DynamicManager32* Handle_ManagerDynamic32Create(size_t elementSize, size_t startingSize, size_t allocationBlockSize);

// dynamic handle manager with external mutux, passing NULL turns off locking
AL2O3_EXTERN_C Handle_DynamicManager32* Handle_ManagerDynamic32CreateWithMutex(size_t elementSize, size_t startingSize, size_t allocationBlockSize, Thread_Mutex* mutex);

AL2O3_EXTERN_C void Handle_DynamicManager32Destroy(Handle_DynamicManager32* manager);

AL2O3_EXTERN_C Handle_DynamicHandle32 Handle_DynamicManager32Alloc(Handle_DynamicManager32* manager);
AL2O3_EXTERN_C void Handle_DynamicManager32Release(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle);

AL2O3_EXTERN_C bool Handle_DynamicManager32IsValid(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle);

// this returns the data ptr for the handle but is unsafe without manual locks for dynamic managers.
AL2O3_EXTERN_C void* Handle_DynamicManager32HandleToPtr(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle);

// when taken dynamic memory allocations (and any potential pointer invalidations) are stalled. Noop for lockless
AL2O3_EXTERN_C void Handle_DynamicManager32Lock(Handle_DynamicManager32* manager);
AL2O3_EXTERN_C void Handle_DynamicManager32Unlock(Handle_DynamicManager32* manager);

// returns how many handles are actually allocated regardless of use/free etc.
AL2O3_EXTERN_C uint32_t Handle_DynamicManager32HandleAllocatedCount(Handle_DynamicManager32* manager);

// control the dynamic manager, handle generation elongation. Uses additional memory by reusing handles less
AL2O3_EXTERN_C void Handle_DynamicManager32SetDeferredFlushThreshold(Handle_DynamicManager32* manager, uint32_t threshold);
AL2O3_EXTERN_C void Handle_DynamicManager32SetDelayedFlushThreshold(Handle_DynamicManager32* manager, uint32_t threshold);

// these are safest copying safely the data to/from then handle data rather than direct access. Takes the mutex lock so stalls
AL2O3_EXTERN_C void Handle_DynamicManager32CopyToMemory(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle, void* dst);
AL2O3_EXTERN_C void Handle_DynamicManager32CopyFromMemory(Handle_DynamicManager32* manager, Handle_DynamicHandle32 handle, void const* src);

#if __cplusplus

// helper for taking and releasing the mutex lock where required
// pretty cheap (call + if) even for lockless managers
struct Handle_DynamicManager32ScopedLock {
	explicit Handle_DynamicManager32ScopedLock(Handle_DynamicManager32* man) : manager(man) {
		Handle_DynamicManager32Lock(manager);
	}

	~Handle_DynamicManager32ScopedLock() {
		Handle_DynamicManager32Unlock(manager);
	}

	/// Prevent copy construction.
	Handle_DynamicManager32ScopedLock(const Handle_DynamicManager32ScopedLock& rhs) = delete;
	/// Prevent assignment.
	Handle_DynamicManager32ScopedLock& operator=(const Handle_DynamicManager32ScopedLock& rhs) = delete;

	Handle_DynamicManager32* manager;
};

#endif