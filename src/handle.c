// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_handle/fixed.h"
#include "al2o3_handle/dynamic.h"
#include "al2o3_handle/handle.h"

// for fixed some function aren't linkable (always inlines) and some do nothing
static bool FixedIsValidFunc(void* actualManager, Handle_Handle32 handle) {
	return Handle_FixedManager32IsValid((Handle_FixedManager32*)actualManager, (Handle_FixedHandle32) handle);
}
static void* FixedHandleToPtrFunc(void* actualManager, Handle_Handle32 handle) {
	return Handle_FixedManager32HandleToPtr((Handle_FixedManager32*)actualManager, (Handle_FixedHandle32) handle);
}
static void FixedCopyToMemoryFunc(void* actualManager, Handle_Handle32 handle, void* dst) {
	Handle_FixedManager32* manager = (Handle_FixedManager32*)actualManager;
	void * src =  Handle_FixedManager32HandleToPtr(manager, (Handle_FixedHandle32) handle);
	memcpy(dst, src, manager->elementSize);
}
static void FixedCopyFromMemoryFunc(void* actualManager, Handle_Handle32 handle, void const* src) {
	Handle_FixedManager32* manager = (Handle_FixedManager32*)actualManager;
	void * dst =  Handle_FixedManager32HandleToPtr(manager, (Handle_FixedHandle32) handle);
	memcpy(dst, src, manager->elementSize);
}
static uint32_t FixedHandleAllocatedCount(void* actualManager) {
	Handle_FixedManager32* manager = (Handle_FixedManager32*)actualManager;
	return manager->totalHandleCount;
}

static Handle_Manager32VTable fixedVTable = {
	.destroy = NULL, // shortcut below
	.alloc = (Handle_Manager32AllocFunc) &Handle_FixedManager32Alloc,
	.release = (Handle_Manager32ReleaseFunc) &Handle_FixedManager32Release,
	.isValid = &FixedIsValidFunc,
	.lock = NULL, // shortcut
	.unlock = NULL, // shortcut
	.handleToPtr = &FixedHandleToPtrFunc,
	.copyToMemory = &FixedCopyToMemoryFunc,
	.copyFromMemory = &FixedCopyFromMemoryFunc,
	.handleAllocatedCount = &FixedHandleAllocatedCount
};

static Handle_Manager32VTable dynamicVTable = {
		.destroy = (Handle_Manager32DestroyFunc) &Handle_DynamicManager32Destroy,
		.alloc = (Handle_Manager32AllocFunc) &Handle_DynamicManager32Alloc,
		.release = (Handle_Manager32ReleaseFunc) &Handle_DynamicManager32Release,
		.isValid = (Handle_Manager32IsValidFunc) &Handle_DynamicManager32IsValid,
		.lock = (Handle_Manager32LockFunc) &Handle_DynamicManager32Lock,
		.unlock = (Handle_Manager32UnlockFunc) &Handle_DynamicManager32Unlock,
		.handleToPtr = (Handle_Manager32HandleToPtrFunc) &Handle_DynamicManager32HandleToPtr,
		.copyToMemory = (Handle_Manager32CopyToMemoryFunc) &Handle_DynamicManager32CopyToMemory,
		.copyFromMemory = (Handle_Manager32CopyFromMemoryFunc) &Handle_DynamicManager32CopyFromMemory,
		.handleAllocatedCount = (Handle_Manager32HandleAllocatedCountFunc) &Handle_DynamicManager32HandleAllocatedCount
};

Handle_Manager32VTable* Handle_Manager32VTableGlobal[256] = {
		&fixedVTable,
		&dynamicVTable
};

AL2O3_EXTERN_C Handle_Manager32* Handle_Manager32CreateFromFixed(Handle_FixedManager32* fixed) {
	Handle_Manager32* manager = (Handle_Manager32*)MEMORY_CALLOC(1, sizeof(Handle_Manager32));
	manager->actualManager = fixed;
	manager->type = 0; // fixed
	manager->isLockless = 1;
	return manager;
}

AL2O3_EXTERN_C Handle_Manager32* Handle_Manager32CreateFromDynamic(Handle_DynamicManager32* dynamic) {
	Handle_Manager32* manager = (Handle_Manager32*)MEMORY_CALLOC(1, sizeof(Handle_Manager32));
	manager->actualManager = dynamic;
	manager->type = 1; // dynamic
	manager->isLockless = 0;
	return manager;
}

AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32* manager) {
	if(!manager) {
		return;
	}
	// short cut for fixed
	if(manager->type == 0) {
		MEMORY_FREE(manager->actualManager);
	} else {
		Handle_Manager32GetVTable(manager->type)->destroy(manager->actualManager);
	}

	MEMORY_FREE(manager);
}
