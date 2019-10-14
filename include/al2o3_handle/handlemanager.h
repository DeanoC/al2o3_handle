#pragma once

#include "al2o3_handle/handle.h"
#include "al2o3_thread/thread.h"

AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32Create(size_t elementSize, size_t allocationBlockSize);
AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32CreateWithMutex(size_t elementSize, size_t allocationBlockSize, Thread_Mutex* mutex);
AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32CreateNoLocks(size_t elementSize, size_t allocationBlockSize);

AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32CreateFixedSize(size_t elementSize, size_t totalHandleCount);

AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32Handle manager);

AL2O3_EXTERN_C Handle_Handle32 Handle_Manager32Alloc(Handle_Manager32Handle manager);
AL2O3_EXTERN_C void Handle_Manager32Release(Handle_Manager32Handle manager, Handle_Handle32 handle);

AL2O3_EXTERN_C bool Handle_Manager32IsValid(Handle_Manager32Handle manager, Handle_Handle32 handle);

// this returns the data ptr for the handle but is unsafe any alloc on another thread could make the pointer
// invalid!
AL2O3_EXTERN_C void* Handle_Manager32ToPtr(Handle_Manager32Handle manager, Handle_Handle32 handle);

// during the lock memory allocations (and potential pointer invalidations are stalled)
AL2O3_EXTERN_C void Handle_Manager32Lock(Handle_Manager32Handle manager);
AL2O3_EXTERN_C void Handle_Manager32Unlock(Handle_Manager32Handle manager);

// fixed and no mutex null out lock/unlock calls, this will return true if no mutex
AL2O3_EXTERN_C bool Handle_Manager32IsLockFree(Handle_Manager32Handle manager);

// these are safest but copy the data to from then handle actual data rather than direct access
AL2O3_EXTERN_C void Handle_Manager32CopyTo(Handle_Manager32Handle manager, Handle_Handle32 handle, void* dst);
AL2O3_EXTERN_C void Handle_Manager32CopyFrom(Handle_Manager32Handle manager, Handle_Handle32 handle, void const* src);

#if __cplusplus
struct Handle_Manager32ScopedLock {
	Handle_Manager32ScopedLock(Handle_Manager32Handle man) : manager(man) {
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