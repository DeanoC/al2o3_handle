#pragma once

#include "al2o3_handle/handle.h"

AL2O3_EXTERN_C Handle_Manager32Handle Handle_Manager32Create(size_t elementSize, size_t allocationBlockSize);
AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32Handle manager);

AL2O3_EXTERN_C Handle_Handle32 Handle_Manager32Alloc(Handle_Manager32Handle manager);
AL2O3_EXTERN_C void Handle_Manager32Release(Handle_Manager32Handle manager, Handle_Handle32 handle);

AL2O3_EXTERN_C bool Handle_Manager32IsValid(Handle_Manager32Handle manager, Handle_Handle32 handle);

// this returns the data ptr for the handle but is unsafe any alloc on another thread could make the pointer
// invalid!
AL2O3_EXTERN_C void* Handle_Manager32ToPtrUnsafe(Handle_Manager32Handle manager, Handle_Handle32 handle);

// these will return data ptr but will block until unlock any allocation/release on another thread
// not Unlock will break everything!
AL2O3_EXTERN_C void* Handle_Manager32ToPtrLock(Handle_Manager32Handle manager, Handle_Handle32 handle);
AL2O3_EXTERN_C void Handle_Manager32ToPtrUnlock(Handle_Manager32Handle manager);

// these are safest but copy the data to from then handle actual data rather than direct access
AL2O3_EXTERN_C void Handle_Manager32CopyTo(Handle_Manager32Handle manager, Handle_Handle32 handle, void* dst);
AL2O3_EXTERN_C void Handle_Manager32CopyFrom(Handle_Manager32Handle manager, Handle_Handle32 handle, void const* src);

