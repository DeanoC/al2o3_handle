#include "al2o3_platform/platform.h"
#include "al2o3_catch2/catch2.hpp"
#include "al2o3_handle/handle.h"
#include "al2o3_handle/handlemanager.h"

struct Test {
	uint8_t data[256];
};

static void FillTest(Test* test) {
	for(int i=0;i < 256;++i) {
		test->data[i] = i;
	}
}

TEST_CASE("Basic tests", "[al2o3 handle]") {
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), 16);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Block allocation tests", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		REQUIRE(handle == i);
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("deferred allocation tests", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	// the first AllocationBlockSize*2 are new gen0 indexes
	// the next AllocationBlockSize are gen1 or gen2
	// the nextAllocationBlockSize are gen0
	// this shows the deferement to break up indexes is working
	for(int i = 0; i < AllocationBlockSize * 4;++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if(i < AllocationBlockSize * 2) {
			REQUIRE(handle == i);
		} else if( i < AllocationBlockSize * 3){
			uint32_t index = AllocationBlockSize - (i - AllocationBlockSize*2) - 1;
			REQUIRE(handle == (0x01000000 | index));
		} else {
			REQUIRE(handle == (i - AllocationBlockSize));
		}
		Handle_Manager32Release(manager, handle);
	}
	// we should hit the deferred backoff limit, so these should all be existing
	// reused handles. the first 48 are reused so not gen0, the last 16 are gen0
	for(int i = 0; i < AllocationBlockSize * 4;++i) {
		if( i < AllocationBlockSize * 2) {
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			REQUIRE((handle & 0xFF000000) != 0);
		} else if( i < AllocationBlockSize * 3){
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			REQUIRE((handle & 0xFF000000) != 0);
		} else {
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			REQUIRE((handle & 0xFF000000) == 0);
		}
	}
	Handle_Manager32Destroy(manager);
}

TEST_CASE("generation tests", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		Handle_Manager32Release(manager, handle);
		REQUIRE(Handle_Manager32IsValid(manager, handle) == false);
	}

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		REQUIRE(Handle_Manager32IsValid(manager, handle) == true);
		Handle_Manager32Release(manager, handle);
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("data access tests", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_Handle32 dataHandle = Handle_Manager32Alloc(manager);
	void * unsafePtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
	// alloc always returns zero'ed data
	REQUIRE(memcmp(unsafePtr, &zeroData, sizeof(Test)) == 0);
	memcpy(unsafePtr, &testData, sizeof(Test));

	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32Release(manager, dataHandle);
	REQUIRE(Handle_Manager32IsValid(manager, dataHandle) == false);
	// release doesn't zero the data but it will be different
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) != 0);

	dataHandle = Handle_Manager32Alloc(manager);
	void* lockedPtr = Handle_Manager32ToPtrLock(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_Manager32ToPtrUnlock(manager);
	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}
	lockedPtr = Handle_Manager32ToPtrLock(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32ToPtrUnlock(manager);

	dataHandle = Handle_Manager32Alloc(manager);
	Handle_Manager32CopyFrom(manager, dataHandle, &testData);
	unsafePtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);

	Test copyTo;
	Handle_Manager32CopyTo(manager, dataHandle, &copyTo);
	REQUIRE(memcmp(&copyTo, &testData, sizeof(Test)) == 0);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Basic tests No Locks", "[al2o3 handle]") {
	Handle_Manager32Handle manager = Handle_Manager32CreateNoLocks(sizeof(Test), 16);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Block allocation tests  No Locks", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateNoLocks(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		REQUIRE(handle == i);
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("deferred allocation tests  No Locks", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateNoLocks(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	// the first AllocationBlockSize*2 are new gen0 indexes
	// the next AllocationBlockSize are gen1 or gen2
	// the nextAllocationBlockSize are gen0
	// this shows the deferement to break up indexes is working
	for(int i = 0; i < AllocationBlockSize * 4;++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if(i < AllocationBlockSize * 2) {
			REQUIRE(handle == i);
		} else if( i < AllocationBlockSize * 3){
			uint32_t index = AllocationBlockSize - (i - AllocationBlockSize*2) - 1;
			REQUIRE(handle == (0x01000000 | index));
		} else {
			REQUIRE(handle == (i - AllocationBlockSize));
		}
		Handle_Manager32Release(manager, handle);
	}
	// we should hit the deferred backoff limit, so these should all be existing
	// reused handles. the first 48 are reused so not gen0, the last 16 are gen0
	for(int i = 0; i < AllocationBlockSize * 4;++i) {
		if( i < AllocationBlockSize * 2) {
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			REQUIRE((handle & 0xFF000000) != 0);
		} else if( i < AllocationBlockSize * 3){
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			REQUIRE((handle & 0xFF000000) != 0);
		} else {
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			REQUIRE((handle & 0xFF000000) == 0);
		}
	}
	Handle_Manager32Destroy(manager);
}

TEST_CASE("generation tests  No Locks", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateNoLocks(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		Handle_Manager32Release(manager, handle);
		REQUIRE(Handle_Manager32IsValid(manager, handle) == false);
	}

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		REQUIRE(Handle_Manager32IsValid(manager, handle) == true);
		Handle_Manager32Release(manager, handle);
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("data access tests  No Locks", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateNoLocks(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_Handle32 dataHandle = Handle_Manager32Alloc(manager);
	void * unsafePtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
	// alloc always returns zero'ed data
	REQUIRE(memcmp(unsafePtr, &zeroData, sizeof(Test)) == 0);
	memcpy(unsafePtr, &testData, sizeof(Test));

	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32Release(manager, dataHandle);
	REQUIRE(Handle_Manager32IsValid(manager, dataHandle) == false);
	// release doesn't zero the data but it will be different
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) != 0);

	dataHandle = Handle_Manager32Alloc(manager);
	void* lockedPtr = Handle_Manager32ToPtrLock(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_Manager32ToPtrUnlock(manager);
	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}
	lockedPtr = Handle_Manager32ToPtrLock(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32ToPtrUnlock(manager);

	dataHandle = Handle_Manager32Alloc(manager);
	Handle_Manager32CopyFrom(manager, dataHandle, &testData);
	unsafePtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);

	Test copyTo;
	Handle_Manager32CopyTo(manager, dataHandle, &copyTo);
	REQUIRE(memcmp(&copyTo, &testData, sizeof(Test)) == 0);

	Handle_Manager32Destroy(manager);
}