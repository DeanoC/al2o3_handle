// License Summary: MIT see LICENSE file
#pragma once

// virtualised interface to either a fixed or dynamic handle manager, useful
// to quick change one to the other for a small runtime cost.
// Most useful to use as a dynamic during dev and switch to fixed for release

// A 32 bit handle can access 16.7 million objects and 256 generations per handle
typedef uint32_t Handle_Handle32;
#define Handle_MaxHandles32 0x00FFFFFF
// Handle_InvalidHandle32 == 0 to help catch clear before alloc bugs
#define Handle_InvalidHandle32 0

typedef struct Handle_FixedManager32 Handle_FixedManager32;
typedef struct Handle_DynamicManager32 Handle_DynamicManager32;

typedef void (*Handle_Manager32DestroyFunc)(void* actualManager);
typedef Handle_Handle32 (*Handle_Manager32AllocFunc)(void* actualManager);
typedef void (*Handle_Manager32ReleaseFunc)(void* actualManager, Handle_Handle32);
typedef bool (*Handle_Manager32IsValidFunc)(void* actualManager, Handle_Handle32 handle);
typedef void* (*Handle_Manager32HandleToPtrFunc)(void* actualManager, Handle_Handle32 handle);
typedef uint32_t (*Handle_Manager32HandleAllocatedCountFunc)(void* actualManager);

typedef struct Handle_Manager32VTable {
	Handle_Manager32DestroyFunc destroy;
	Handle_Manager32AllocFunc alloc;
	Handle_Manager32ReleaseFunc release;
	Handle_Manager32IsValidFunc isValid;
	Handle_Manager32HandleToPtrFunc handleToPtr;
	Handle_Manager32HandleAllocatedCountFunc handleAllocatedCount;
} Handle_Manager32VTable;

typedef struct Handle_Manager32 {
	void* actualManager;
	uint8_t type; // 0 = fixed, 1 = dynamic, 2+ available for others
	uint8_t	isLockless; // FALSE is lock calla can be ommited
} Handle_Manager32;

// global vtable register, used function below to modify and get
AL2O3_EXTERN_C Handle_Manager32VTable* Handle_Manager32VTableGlobal[256];

// vtable pointers must stay alive for the duration of calls into Handle_Manager(can be reset to NULL)
AL2O3_FORCE_INLINE void Handle_Manager32RegisterVTable(uint8_t type, Handle_Manager32VTable* vtable) {
	ASSERT(!Handle_Manager32VTableGlobal[type]);
	Handle_Manager32VTableGlobal[type] = vtable;
}

AL2O3_FORCE_INLINE Handle_Manager32VTable* Handle_Manager32GetVTable(uint8_t type) {
	ASSERT(Handle_Manager32VTableGlobal[type]);
	return Handle_Manager32VTableGlobal[type];
}

AL2O3_EXTERN_C Handle_Manager32* Handle_Manager32CreateFromFixed(Handle_FixedManager32* fixed);
AL2O3_EXTERN_C Handle_Manager32* Handle_Manager32CreateFromDynamic(Handle_DynamicManager32* dynamic);
AL2O3_EXTERN_C void Handle_Manager32Destroy(Handle_Manager32* manager);

AL2O3_FORCE_INLINE Handle_Handle32 Handle_Manager32Alloc(Handle_Manager32* manager) {
	return Handle_Manager32GetVTable(manager->type)->alloc(manager->actualManager);
}

AL2O3_FORCE_INLINE void Handle_Manager32Release(Handle_Manager32* manager, Handle_Handle32 handle) {
	Handle_Manager32GetVTable(manager->type)->release(manager->actualManager, handle);
}

AL2O3_FORCE_INLINE bool Handle_Manager32IsValid(Handle_Manager32* manager, Handle_Handle32 handle) {
	return Handle_Manager32GetVTable(manager->type)->isValid(manager->actualManager, handle);
}

AL2O3_FORCE_INLINE void* Handle_Manager32HandleToPtr(Handle_Manager32* manager, Handle_Handle32 handle) {
	return Handle_Manager32GetVTable(manager->type)->handleToPtr(manager->actualManager, handle);
}

AL2O3_FORCE_INLINE uint32_t Handle_Manager32HandleAllocatedCount(Handle_Manager32* manager) {
	return Handle_Manager32GetVTable(manager->type)->handleAllocatedCount(manager->actualManager);
}
