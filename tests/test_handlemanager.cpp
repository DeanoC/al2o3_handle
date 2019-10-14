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

static void TestGenerations(int const AllocationBlockSize, Handle_Manager32 *manager) {
	// | blk  |  Gen  | Def |
	// |------|-------|-----|
	// | 0    |   0   | 0   |
	// | 2    |   N   | 2   | 2
	// | 3    |   0   | 3   | 1
	// | 4    |   N   | 3   | 1
	// | 6    |   0   | 4   | 2
	// | 7    |   N   | 0   | 1 --
	// | 14   |   0   | 1   | 7
	// | 15   |   N   | 1   | 1
	// | 19   |   0   | 2   | 4
	// | 20   |   N   | 2   | 1
	// | 25   |   0   | 3   | 5
	// | 26   |   N   | 3   | 1
	// | 32   |   0   | 4   | 6
	// | 33   |   N   | 0   | 1 --
	// | 48   |   0   | 1   | 15
	// | 49   |   N   | 1   | 1
	uint32_t const blockTestTable[15] = {
			2, 3, 4, 6, 7, 14, 15, 19, 20, 25, 26, 32, 33, 48, 49
	};

	bool shouldBeZeroGen = true;
	uint32_t start = 0;
	for(int i = 0; i < 15;++i) {
		uint32_t end = blockTestTable[i];

		for(uint32_t j = start * AllocationBlockSize; j < end * AllocationBlockSize;++j) {
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			bool isZeroGen = (handle & 0xFF000000) == 0;
			//			LOGINFO("%i : %i expected %i", j / AllocationBlockSize, handle >> 24, !shouldBeZeroGen);
			REQUIRE(isZeroGen == shouldBeZeroGen);
			Handle_Manager32Release(manager, handle);
		}

		shouldBeZeroGen ^= true;
		start = end;
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

	TestGenerations(AllocationBlockSize, manager);

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
	Handle_Manager32Lock(manager);
	void* lockedPtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_Manager32ToPtrUnlock(manager);

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}

	Handle_Manager32Lock(manager);
	lockedPtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
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

	TestGenerations(AllocationBlockSize, manager);

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
	Handle_Manager32Lock(manager);
	void* lockedPtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_Manager32ToPtrUnlock(manager);

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}

	Handle_Manager32Lock(manager);
	lockedPtr = Handle_Manager32ToPtrUnsafe(manager, dataHandle);
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

TEST_CASE("Basic tests Fixed", "[al2o3 handle]") {
	Handle_Manager32Handle manager = Handle_Manager32FixedSize(sizeof(Test), 16);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("generation tests Fixed", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateNoLocks(sizeof(Test), AllocationBlockSize*4);
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

TEST_CASE("data access tests Fixed", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize * 4);
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

