#pragma once

#include "al2o3_handle/handle.h"
#include "al2o3_thread/thread.h"

// dynamic handle manager with internal mutex used when required
AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32Create(size_t elementSize, size_t startingSize, size_t allocationBlockSize);

// dynamic handle manager with external mutux, passing NULL turns off locking
AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32CreateWithMutex(size_t elementSize, size_t startingSize, size_t allocationBlockSize, Thread_Mutex* mutex);

// fixed size handle manager. Multithread safe via CAS atomics
AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32CreateFixedSize(size_t elementSize, size_t totalHandleCount);

AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32Handle manager);

AL2O3_EXTERN_C Handle_Handle32 Handle_Manager32Alloc(Handle_Manager32Handle manager);
AL2O3_EXTERN_C void Handle_Manager32Release(Handle_Manager32Handle manager, Handle_Handle32 handle);

AL2O3_EXTERN_C bool Handle_Manager32IsValid(Handle_Manager32Handle manager, Handle_Handle32 handle);

// this returns the data ptr for the handle but is unsafe without manual locks for dynamic managers.
AL2O3_EXTERN_C void* Handle_Manager32ToPtr(Handle_Manager32Handle manager, Handle_Handle32 handle);

// when taken dynamic memory allocations (and any potential pointer invalidations) are stalled. Noop for lockless
AL2O3_EXTERN_C void Handle_Manager32Lock(Handle_Manager32Handle manager);
AL2O3_EXTERN_C void Handle_Manager32Unlock(Handle_Manager32Handle manager);

// fixed and no mutex null out lock/unlock calls, this will return true if no mutex are harmed during production
AL2O3_EXTERN_C bool Handle_Manager32IsLockFree(Handle_Manager32Handle manager);

// returns how many handles are actually allocated regardless of use/free etc.
AL2O3_EXTERN_C uint32_t Handle_Manager32HandleAllocatedCount(Handle_Manager32Handle manager);

// control the dynamic manager, handle generation elongation. Uses additional memory by reusing handles less
AL2O3_EXTERN_C void Handle_Manager32SetDeferredFlushThreshold(Handle_Manager32Handle manager, uint32_t threshold);
AL2O3_EXTERN_C void Handle_Manager32SetDelayedFlushThreshold(Handle_Manager32Handle manager, uint32_t threshold);

// these are safest copying safely the data to/from then handle data rather than direct access. Takes the mutex lock so stalls
AL2O3_EXTERN_C void Handle_Manager32CopyTo(Handle_Manager32Handle manager, Handle_Handle32 handle, void* dst);
AL2O3_EXTERN_C void Handle_Manager32CopyFrom(Handle_Manager32Handle manager, Handle_Handle32 handle, void const* src);

#if __cplusplus

// helper for taking and releasing the mutex lock where required
// pretty cheap (call + if) even for lockless managers
struct Handle_Manager32ScopedLock {
	explicit Handle_Manager32ScopedLock(Handle_Manager32Handle man) : manager(man) {
		Handle_Manager32Lock(manager);
	}

	~Handle_Manager32ScopedLock() {
		Handle_Manager32Unlock(manager);
	}

	/// Prevent copy construction.
	Handle_Manager32ScopedLock(const Handle_Manager32ScopedLock& rhs) = delete;
	/// Prevent assignment.
	Handle_Manager32ScopedLock& operator=(const Handle_Manager32ScopedLock& rhs) = delete;

	Handle_Manager32Handle manager;
};

#endif