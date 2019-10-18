// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_handle/fixed.h"
#include "al2o3_handle/dynamic.h"
#include "al2o3_handle/handle.h"

// for fixed some function aren't linkable (always inlines) and some do nothing
static bool FixedIsValid(void* actualManager, Handle_Handle32 handle) {
	return Handle_FixedManager32IsValid((Handle_FixedManager32*)actualManager, (Handle_FixedHandle32) handle);
}
static void* FixedHandleToPtr(void* actualManager, Handle_Handle32 handle) {
	return Handle_FixedManager32HandleToPtr((Handle_FixedManager32*)actualManager, (Handle_FixedHandle32) handle);
}
static void FixedCopyToMemory(void* actualManager, Handle_Handle32 handle, void* dst) {
	Handle_FixedManager32* manager = (Handle_FixedManager32*)actualManager;
	void * src =  Handle_FixedManager32HandleToPtr(manager, (Handle_FixedHandle32) handle);
	memcpy(dst, src, manager->elementSize);
}
static void FixedCopyFromMemory(void* actualManager, Handle_Handle32 handle, void const* src) {
	Handle_FixedManager32* manager = (Handle_FixedManager32*)actualManager;
	void * dst =  Handle_FixedManager32HandleToPtr(manager, (Handle_FixedHandle32) handle);
	memcpy(dst, src, manager->elementSize);
}
static uint32_t FixedHandleAllocatedCount(void* actualManager) {
	Handle_FixedManager32* manager = (Handle_FixedManager32*)actualManager;
	return manager->totalHandleCount;
}
static bool DynamicIsValid(void* actualManager, Handle_Handle32 handle) {
	return Handle_DynamicManager32IsValid((Handle_DynamicManager32*)actualManager, (Handle_FixedHandle32) handle);
}
static void* DynamicHandleToPtr(void* actualManager, Handle_Handle32 handle) {
	return Handle_DynamicManager32HandleToPtr((Handle_DynamicManager32*)actualManager, (Handle_FixedHandle32) handle);
}
static uint32_t DynamicHandleAllocatedCount(void* actualManager) {
	Handle_DynamicManager32* manager = (Handle_DynamicManager32*)actualManager;
	return Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated);
}

static Handle_Manager32VTable fixedVTable = {
	.destroy = NULL, // shortcut below
	.alloc = (Handle_Manager32AllocFunc) &Handle_FixedManager32Alloc,
	.release = (Handle_Manager32ReleaseFunc) &Handle_FixedManager32Release,
	.isValid = &FixedIsValid,
	.handleToPtr = &FixedHandleToPtr,
	.handleAllocatedCount = &FixedHandleAllocatedCount
};

static Handle_Manager32VTable dynamicVTable = {
		.destroy = (Handle_Manager32DestroyFunc) &Handle_DynamicManager32Destroy,
		.alloc = (Handle_Manager32AllocFunc) &Handle_DynamicManager32Alloc,
		.release = (Handle_Manager32ReleaseFunc) &Handle_DynamicManager32Release,
		.isValid = (Handle_Manager32IsValidFunc) &DynamicIsValid,
		.handleToPtr = (Handle_Manager32HandleToPtrFunc) &DynamicHandleToPtr,
		.handleAllocatedCount = &DynamicHandleAllocatedCount
};

AL2O3_EXTERN_C Handle_Manager32VTable* Handle_Manager32VTableGlobal[256] = {
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
