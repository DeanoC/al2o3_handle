#include "al2o3_platform/platform.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_catch2/catch2.hpp"
#include "al2o3_handle/handle.h"
#include "al2o3_handle/handlemanager.h"
#include "al2o3_cadt/vector.h"

#include <inttypes.h>
struct Test {
	uint8_t data[256];
};

static void FillTest(Test* test) {
	for(int i=0;i < 256;++i) {
		test->data[i] = i;
	}
}

static void TestGenerations(int const AllocationBlockSize, Handle_Manager32 *manager) {
	// all alloc numbers are in block count
	// | blk  |  Gen  | Def | Alloc | diff  | flush |
	// |------|-------|-----|-------|-------|-------|
	// | 0    |   0   | 0   |  2    |   0   |       |
	// | 2    |   N   | 2   |  2    |   2   |       |
	// | 3    |   0   | 3   |  3    |   1   |       |
	// | 4    |   N   | 3   |  3    |   1   |       |
	// | 6    |   0   | 4   |  4    |   2   |       |
	// | 7    |   N   | 0   |  4    |   1   |  ---  |
	// | 14   |   0   | 1   |  5    |   7   |       |
	// | 15   |   N   | 1   |  5    |   1   |       |
	// | 19   |   0   | 2   |  6    |   4   |       |
	// | 20   |   N   | 2   |  6    |   1   |       |
	// | 25   |   0   | 3   |  7    |   5   |       |
	// | 26   |   N   | 3   |  7    |   1   |       |
	// | 32   |   0   | 4   |  8    |   6   |       |
	// | 33   |   N   | 0   |  8    |   1   |  ---  |
	// | 48   |   0   | 1   |  9    |  15   |       |
	// | 49   |   N   | 1   |  9    |   1   |       |
	// |--------------------------------------------|
	Handle_Manager32SetDeferredFlushThreshold(manager, 4);
	Handle_Manager32SetDelayedFlushThreshold(manager, 100);

	uint32_t const blockTestTable[15] = {
			2, 3, 4, 6, 7, 14, 15, 19, 20, 25, 26, 32, 33, 48, 49
	};

	bool shouldBeZeroGen = true;
	uint32_t start = 0;
	for(int i = 0; i < 15;++i) {
		uint32_t end = blockTestTable[i];

		for(uint32_t j = start * AllocationBlockSize; j < end * AllocationBlockSize;++j) {
			Handle_Handle32 handle = Handle_Manager32Alloc(manager);
			bool const isZeroGen = (handle >> 24) == 0;

			// 0th index is 1 gen old to make 0 handle illegal
			if(j == 0) {
				REQUIRE(isZeroGen == false);
			} else {
//				LOGINFO("0x%x %i : %i expected %i", handle, j / AllocationBlockSize, handle >> 24, !shouldBeZeroGen);
				REQUIRE(isZeroGen == shouldBeZeroGen);
			}
			Handle_Manager32Release(manager, handle);
		}

		shouldBeZeroGen ^= true;
		start = end;
	}
}


TEST_CASE("Basic tests", "[al2o3 handle]") {
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), 16, 16);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Block allocation tests", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if( i == 0) {
			REQUIRE(handle == 0x01000000);
		} else {
			REQUIRE(handle == i);
		}
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("deferred allocation tests", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(manager);

	TestGenerations(AllocationBlockSize, manager);

	Handle_Manager32Destroy(manager);
}


TEST_CASE("generation tests", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
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
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_Handle32 dataHandle = Handle_Manager32Alloc(manager);
	void * unsafePtr = Handle_Manager32ToPtr(manager, dataHandle);
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
	void* lockedPtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_Manager32Unlock(manager);

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}

	Handle_Manager32Lock(manager);
	lockedPtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32Unlock(manager);

	dataHandle = Handle_Manager32Alloc(manager);
	Handle_Manager32CopyFrom(manager, dataHandle, &testData);
	unsafePtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);

	Test copyTo;
	Handle_Manager32CopyTo(manager, dataHandle, &copyTo);
	REQUIRE(memcmp(&copyTo, &testData, sizeof(Test)) == 0);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Basic tests No Locks", "[al2o3 handle]") {
	Handle_Manager32Handle manager = Handle_Manager32CreateWithMutex(sizeof(Test), 16, 16, NULL);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Block allocation tests  No Locks", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if( i == 0) {
			REQUIRE(handle == 0x01000000);
		} else {
			REQUIRE(handle == i);
		}
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("deferred allocation tests  No Locks", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
	REQUIRE(manager);

	TestGenerations(AllocationBlockSize, manager);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("generation tests  No Locks", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
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
	Handle_Manager32Handle manager = Handle_Manager32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_Handle32 dataHandle = Handle_Manager32Alloc(manager);
	void * unsafePtr = Handle_Manager32ToPtr(manager, dataHandle);
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
	void* lockedPtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_Manager32Unlock(manager);

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}

	Handle_Manager32Lock(manager);
	lockedPtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32Unlock(manager);

	dataHandle = Handle_Manager32Alloc(manager);
	Handle_Manager32CopyFrom(manager, dataHandle, &testData);
	unsafePtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);

	Test copyTo;
	Handle_Manager32CopyTo(manager, dataHandle, &copyTo);
	REQUIRE(memcmp(&copyTo, &testData, sizeof(Test)) == 0);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Basic tests Fixed", "[al2o3 handle]") {
	Handle_Manager32Handle manager = Handle_Manager32CreateFixedSize(sizeof(Test), 16);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("generation tests Fixed", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateFixedSize(sizeof(Test), AllocationBlockSize*4);
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
TEST_CASE("Run out of handles test Fixed", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32CreateFixedSize(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize; ++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
	}
	// there will be a warning log telling us we have run out but it will return invalid handle
	LOGINFO("The next WARN is expected as we are testing the invalid value is return");
	REQUIRE( Handle_Manager32Alloc(manager) == Handle_InvalidHandle32);

}

TEST_CASE("data access tests Fixed", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize*4, AllocationBlockSize * 4);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_Handle32 dataHandle = Handle_Manager32Alloc(manager);
	void * unsafePtr = Handle_Manager32ToPtr(manager, dataHandle);
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
	void* lockedPtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_Manager32Unlock(manager);

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}

	Handle_Manager32Lock(manager);
	lockedPtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32Unlock(manager);

	dataHandle = Handle_Manager32Alloc(manager);
	Handle_Manager32CopyFrom(manager, dataHandle, &testData);
	unsafePtr = Handle_Manager32ToPtr(manager, dataHandle);
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);

	Test copyTo;
	Handle_Manager32CopyTo(manager, dataHandle, &copyTo);
	REQUIRE(memcmp(&copyTo, &testData, sizeof(Test)) == 0);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Generation overflow stats", "[al2o3 handle]") {

	static const uint32_t startingSize = 1024 * 1;
	static const uint32_t blockSize = 16;
	static const uint32_t deferredFlush = 2;
	static const uint32_t delayedFlush = 100;
#if NDEBUG
	static const uint64_t totalAllocReleaseCycles = 1000000000ull;
#else
	static const uint64_t totalAllocReleaseCycles = 100000000ull;
#endif
	Handle_Manager32Handle manager = Handle_Manager32CreateWithMutex(sizeof(Test), startingSize, blockSize, NULL);
	REQUIRE(manager);

	Handle_Manager32SetDeferredFlushThreshold(manager, deferredFlush);
	Handle_Manager32SetDelayedFlushThreshold(manager, delayedFlush);

	uint64_t allocReleaseCycles = 0;
	uint64_t distance = 0;
	uint64_t numDistances = 0;
	CADT_VectorHandle allocTracker = CADT_VectorCreate(sizeof(uint64_t));
	CADT_VectorReserve(allocTracker, 100000);

	Handle_Manager32Alloc(manager); // remove 0 index from this test
	CADT_VectorPushElement(allocTracker, &allocReleaseCycles);

	while(allocReleaseCycles != totalAllocReleaseCycles ) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		// we use 1st gen due to the special case of handle 0
		if((handle >> 24u) == 1) {
			uint32_t const index = (handle & 0x00FFFFFFu);
			if(index < CADT_VectorSize(allocTracker)) {
				distance += allocReleaseCycles - *((uint64_t*)CADT_VectorAt(allocTracker, index));
				numDistances++;
				*(uint64_t*)CADT_VectorAt(allocTracker,index) = allocReleaseCycles;
			} else {
				CADT_VectorResize(allocTracker, index + 1);
				*(uint64_t*)CADT_VectorAt(allocTracker, index) = allocReleaseCycles;
			}
		}
		Handle_Manager32Release(manager, handle);
		allocReleaseCycles++;
	}

	LOGINFO("After %" PRId64 " million alloc/release cycles", totalAllocReleaseCycles / 1000000ull);
	uint32_t allocated = Handle_Manager32HandleAllocatedCount(manager);
	if(numDistances == 0) {
		LOGINFO("No handle generation overflow has occured");
	} else {
		LOGINFO("Average distance between handle generation reuse %i", distance / numDistances);
	}
	LOGINFO("Objects allocated (initial pool size was %i) %u", startingSize + blockSize, allocated);
	LOGINFO("Memory overhead %u B", (allocated - startingSize) * sizeof(uint64_t));

	CADT_VectorDestroy(allocTracker);
	Handle_Manager32Destroy(manager);
}

Thread_Atomic64_t leaked = {0};
static void InternalThreadFunc(Handle_Manager32Handle manager, uint64_t totalAllocReleaseCycles ) {

	uint64_t allocReleaseCycles = 0;

	while(allocReleaseCycles != totalAllocReleaseCycles ) {

		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if(handle == Handle_InvalidHandle32) {
			return;
		}

		Handle_Manager32CopyFrom(manager, handle, &allocReleaseCycles);
		uint64_t test;
		for (auto i = 0u; i < 100; ++i) {
			Handle_Manager32CopyTo(manager, handle, &test);
			if(test != allocReleaseCycles) {
				LOGINFO("%" PRId64 "  %" PRId64, allocReleaseCycles, test);
			}
		}
		// every now and again don't release the handle to mix things up
		if((allocReleaseCycles % 1000) != 0) {
			Handle_Manager32Release(manager, handle);
		} else {
			Thread_AtomicFetchAdd64Relaxed(&leaked, 1);
		}
		allocReleaseCycles++;
	}
}


static void ThreadFuncDynamic(void* userPtr) {
#if NDEBUG
	static const uint64_t totalAllocReleaseCycles = 100000ull;
#else
	static const uint64_t totalAllocReleaseCycles = 10000ull;
#endif

	Handle_Manager32Handle manager = (Handle_Manager32Handle) userPtr;
	InternalThreadFunc(manager, totalAllocReleaseCycles);
}

static void ThreadFuncFixed(void* userPtr) {
#if NDEBUG
	static const uint64_t totalAllocReleaseCycles = 1000000ull;
#else
	static const uint64_t totalAllocReleaseCycles = 1000000ull;
#endif

	Handle_Manager32Handle manager = (Handle_Manager32Handle) userPtr;
	InternalThreadFunc(manager, totalAllocReleaseCycles);
}

TEST_CASE(" Multithreaded", "[al2o3 handle]") {

	LOGINFO("Starting multithread stress handle manager test - takes a while");
	static const uint32_t startingSize = 1024 * 1;
	static const uint32_t blockSize = 16;
	const uint32_t numThreads = Thread_CPUCoreCount() * 5;
	Handle_Manager32Handle manager = Handle_Manager32Create(sizeof(uint64_t), startingSize, blockSize);
	REQUIRE(manager);

	Thread_AtomicStore64Relaxed(&leaked, 0);

	Thread_Thread * threads = (Thread_Thread *)STACK_ALLOC(sizeof(Thread_Thread) * numThreads);

	for (auto i = 0u; i < numThreads; ++i) {
		Thread_ThreadCreate(threads + i, &ThreadFuncDynamic, manager);
	}

	for (auto i = 0u; i < numThreads; ++i) {
		Thread_ThreadJoin(threads + i);
		Thread_ThreadDestroy(threads + i);
	}
	uint64_t leakedCount = Thread_AtomicLoad64Relaxed(&leaked);
	LOGINFO("Delibrately Leaked %" PRId64 " Objects", leakedCount);

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Multithreaded Fixed", "[al2o3 handle]") {

	LOGINFO("Starting multithread stress fixed handle manager test - takes a while");

	static const uint32_t totalSize = 1024 * 50;
	static const uint32_t numThreads = Thread_CPUCoreCount() * 5;
	Handle_Manager32Handle manager = Handle_Manager32CreateFixedSize(sizeof(uint64_t), totalSize);
	REQUIRE(manager);

	Thread_AtomicStore64Relaxed(&leaked, 0);

	Thread_Thread * threads = (Thread_Thread *)STACK_ALLOC(sizeof(Thread_Thread) * numThreads);

	for (auto i = 0u; i < numThreads; ++i) {
		Thread_ThreadCreate(threads + i, &ThreadFuncFixed, manager);
	}

	for (auto i = 0u; i < numThreads; ++i) {
		Thread_ThreadJoin(threads + i);
		Thread_ThreadDestroy(threads + i);
	}
	uint64_t leakedCount = Thread_AtomicLoad64Relaxed(&leaked);
	LOGINFO("Delibrately Leaked %" PRId64 " Objects", leakedCount);
	Handle_Manager32Destroy(manager);
}
