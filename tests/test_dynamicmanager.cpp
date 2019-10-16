// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_catch2/catch2.hpp"
#include "al2o3_handle/dynamic.h"
#include "al2o3_cadt/vector.h"

#include <inttypes.h>
namespace {
struct Test {
	uint8_t data[256];
};

void FillTest(Test *test) {
	for (int i = 0; i < 256; ++i) {
		test->data[i] = i;
	}
}

void TestGenerations(int const AllocationBlockSize, Handle_DynamicManager32 *manager) {
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
	Handle_DynamicManager32SetDeferredFlushThreshold(manager, 4);
	Handle_DynamicManager32SetDelayedFlushThreshold(manager, 100);

	uint32_t const blockTestTable[15] = {
			2, 3, 4, 6, 7, 14, 15, 19, 20, 25, 26, 32, 33, 48, 49
	};

	bool shouldBeZeroGen = true;
	uint32_t start = 0;
	for (int i = 0; i < 15; ++i) {
		uint32_t end = blockTestTable[i];

		for (uint32_t j = start * AllocationBlockSize; j < end * AllocationBlockSize; ++j) {
			Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
			bool const isZeroGen = (handle >> 24) == 0;

			// 0th index is 1 gen old to make 0 handle illegal
			if (j == 0) {
				REQUIRE(isZeroGen == false);
			} else {
				//				LOGINFO("0x%x %i : %i expected %i", handle, j / AllocationBlockSize, handle >> 24, !shouldBeZeroGen);
				REQUIRE(isZeroGen == shouldBeZeroGen);
			}
			Handle_DynamicManager32Release(manager, handle);
		}

		shouldBeZeroGen ^= true;
		start = end;
	}
}
} // end anon namespace
TEST_CASE("Basic tests Dynamic", "[al2o3 handle]") {
	Handle_DynamicManager32* manager = Handle_ManagerDynamic32Create(sizeof(Test), 16, 16);
	REQUIRE(manager);

	Handle_DynamicHandle32 handle0 = Handle_DynamicManager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_DynamicManager32Release(manager, handle0);
	Handle_DynamicHandle32 handle1 = Handle_DynamicManager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_DynamicManager32Release(manager, handle1);

	Handle_DynamicManager32Destroy(manager);
}


TEST_CASE("Block allocation tests Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		if( i == 0) {
			REQUIRE(handle == 0x01000000);
		} else {
			REQUIRE(handle == i);
		}
	}

	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("deferred allocation tests Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(manager);

	TestGenerations(AllocationBlockSize, manager);

	Handle_DynamicManager32Destroy(manager);
}


TEST_CASE("generation tests Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		Handle_DynamicManager32Release(manager, handle);
		REQUIRE(Handle_DynamicManager32IsValid(manager, handle) == false);
	}

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		REQUIRE(Handle_DynamicManager32IsValid(manager, handle) == true);
		Handle_DynamicManager32Release(manager, handle);
	}

	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("data access tests Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_DynamicHandle32 dataHandle = Handle_DynamicManager32Alloc(manager);
	void * unsafePtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	// alloc always returns zero'ed data
	REQUIRE(memcmp(unsafePtr, &zeroData, sizeof(Test)) == 0);
	memcpy(unsafePtr, &testData, sizeof(Test));

	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);
	Handle_DynamicManager32Release(manager, dataHandle);
	REQUIRE(Handle_DynamicManager32IsValid(manager, dataHandle) == false);
	// release doesn't zero the data but it will be different
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) != 0);

	dataHandle = Handle_DynamicManager32Alloc(manager);
	Handle_DynamicManager32Lock(manager);
	void* lockedPtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_DynamicManager32Unlock(manager);

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_DynamicManager32Alloc(manager);
	}

	Handle_DynamicManager32Lock(manager);
	lockedPtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &testData, sizeof(Test)) == 0);
	Handle_DynamicManager32Unlock(manager);

	dataHandle = Handle_DynamicManager32Alloc(manager);
	Handle_DynamicManager32CopyFromMemory(manager, dataHandle, &testData);
	unsafePtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);

	Test copyTo;
	Handle_DynamicManager32CopyToMemory(manager, dataHandle, &copyTo);
	REQUIRE(memcmp(&copyTo, &testData, sizeof(Test)) == 0);

	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("Basic tests No Locks Dynamic", "[al2o3 handle]") {
	Handle_DynamicManager32* manager = Handle_ManagerDynamic32CreateWithMutex(sizeof(Test), 16, 16, NULL);
	REQUIRE(manager);

	Handle_DynamicHandle32 handle0 = Handle_DynamicManager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_DynamicManager32Release(manager, handle0);
	Handle_DynamicHandle32 handle1 = Handle_DynamicManager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_DynamicManager32Release(manager, handle1);

	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("Block allocation tests No Locks Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		if( i == 0) {
			REQUIRE(handle == 0x01000000);
		} else {
			REQUIRE(handle == i);
		}
	}

	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("deferred allocation tests No Locks Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
	REQUIRE(manager);

	TestGenerations(AllocationBlockSize, manager);

	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("generation tests No Locks Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		Handle_DynamicManager32Release(manager, handle);
		REQUIRE(Handle_DynamicManager32IsValid(manager, handle) == false);
	}

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		REQUIRE(Handle_DynamicManager32IsValid(manager, handle) == true);
		Handle_DynamicManager32Release(manager, handle);
	}

	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("data access tests No Locks Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_ManagerDynamic32CreateWithMutex(sizeof(Test), AllocationBlockSize, AllocationBlockSize, NULL);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_DynamicHandle32 dataHandle = Handle_DynamicManager32Alloc(manager);
	void * unsafePtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	// alloc always returns zero'ed data
	REQUIRE(memcmp(unsafePtr, &zeroData, sizeof(Test)) == 0);
	memcpy(unsafePtr, &testData, sizeof(Test));

	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);
	Handle_DynamicManager32Release(manager, dataHandle);
	REQUIRE(Handle_DynamicManager32IsValid(manager, dataHandle) == false);
	// release doesn't zero the data but it will be different
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) != 0);

	dataHandle = Handle_DynamicManager32Alloc(manager);
	Handle_DynamicManager32Lock(manager);
	void* lockedPtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &zeroData, sizeof(Test)) == 0);
	memcpy(lockedPtr, &testData, sizeof(Test));
	Handle_DynamicManager32Unlock(manager);

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_DynamicManager32Alloc(manager);
	}

	Handle_DynamicManager32Lock(manager);
	lockedPtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	REQUIRE(memcmp(lockedPtr, &testData, sizeof(Test)) == 0);
	Handle_DynamicManager32Unlock(manager);

	dataHandle = Handle_DynamicManager32Alloc(manager);
	Handle_DynamicManager32CopyFromMemory(manager, dataHandle, &testData);
	unsafePtr = Handle_DynamicManager32HandleToPtr(manager, dataHandle);
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);

	Test copyTo;
	Handle_DynamicManager32CopyToMemory(manager, dataHandle, &copyTo);
	REQUIRE(memcmp(&copyTo, &testData, sizeof(Test)) == 0);

	Handle_DynamicManager32Destroy(manager);
}

static Thread_Atomic64_t leaked = {0};
static void InternalThreadFunc(Handle_DynamicManager32* manager, uint64_t totalAllocReleaseCycles ) {

	uint64_t allocReleaseCycles = 0;

	while(allocReleaseCycles != totalAllocReleaseCycles ) {

		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		if(handle == Handle_InvalidDynamicHandle32) {
			return;
		}

		Handle_DynamicManager32CopyFromMemory(manager, handle, &allocReleaseCycles);
		uint64_t test;
		for (auto i = 0u; i < 100; ++i) {
			Handle_DynamicManager32CopyToMemory(manager, handle, &test);
			if(test != allocReleaseCycles) {
				LOGINFO("%" PRId64 "  %" PRId64, allocReleaseCycles, test);
			}
		}
		// every now and again don't release the handle to mix things up
		if((allocReleaseCycles % 1000) != 0) {
			Handle_DynamicManager32Release(manager, handle);
		} else {
			Thread_AtomicFetchAdd64Relaxed(&leaked, 1);
		}
		allocReleaseCycles++;
	}
}

#if NDEBUG
static const uint64_t totalAllocReleaseCycles = 100000ull;
#else
static const uint64_t totalAllocReleaseCycles = 10000ull;
#endif

static void ThreadFuncDynamic(void* userPtr) {

	Handle_DynamicManager32* manager = (Handle_DynamicManager32*) userPtr;
	InternalThreadFunc(manager, totalAllocReleaseCycles);
}

TEST_CASE(" Multithreaded Dynamic", "[al2o3 handle]") {

	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting multithread stress dynamic handle manager test - takes a while");
	const uint32_t startingSize = 1024 * 1;
	const uint32_t blockSize = 16;
	const uint32_t numThreads = Thread_CPUCoreCount() * 5;
	const uint64_t totalTotalAllocReleaseCycles = totalAllocReleaseCycles * numThreads;
	const uint32_t totalSize = (totalTotalAllocReleaseCycles/ 1000) + (numThreads*2);

	Handle_DynamicManager32* manager = Handle_ManagerDynamic32Create(sizeof(uint64_t), startingSize, blockSize);
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
	LOGINFO("After %" PRId64 " million alloc/release cycles", totalTotalAllocReleaseCycles / 1000000ull);

	LOGINFO("Delibrately Leaked %" PRId64 " Objects, expected leak count %" PRId64,
					leakedCount,
					totalTotalAllocReleaseCycles / 1000);
	LOGINFO("Total handles allocated (including leaks) %u", Handle_DynamicManager32HandleAllocatedCount(manager));
	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("Generation overflow stats Dynamic", "[al2o3 handle]") {
	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting generation overflow dynamic handle manager test - takes a while");

	static const uint32_t startingSize = 1024 * 1;
	static const uint32_t blockSize = 16;
	static const uint32_t deferredFlush = 2;
	static const uint32_t delayedFlush = 100;
#if NDEBUG
	static const uint64_t totalAllocReleaseCycles = 1000000000ull;
#else
	static const uint64_t totalAllocReleaseCycles = 100000000ull;
#endif
	Handle_DynamicManager32* manager = Handle_ManagerDynamic32CreateWithMutex(sizeof(Test), startingSize, blockSize, NULL);
	REQUIRE(manager);

	Handle_DynamicManager32SetDeferredFlushThreshold(manager, deferredFlush);
	Handle_DynamicManager32SetDelayedFlushThreshold(manager, delayedFlush);

	uint64_t allocReleaseCycles = 0;
	uint64_t distance = 0;
	uint64_t numDistances = 0;
	CADT_VectorHandle allocTracker = CADT_VectorCreate(sizeof(uint64_t));
	CADT_VectorReserve(allocTracker, 100000);

	Handle_DynamicManager32Alloc(manager); // remove 0 index from this test
	CADT_VectorPushElement(allocTracker, &allocReleaseCycles);

	while(allocReleaseCycles != totalAllocReleaseCycles ) {
		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
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
		Handle_DynamicManager32Release(manager, handle);
		allocReleaseCycles++;
	}

	LOGINFO("After %" PRId64 " million alloc/release cycles", totalAllocReleaseCycles / 1000000ull);
	uint32_t allocated = Handle_DynamicManager32HandleAllocatedCount(manager);
	if(numDistances == 0) {
		LOGINFO("No handle generation overflow has occured");
	} else {
		LOGINFO("Average distance between handle generation reuse %i", distance / numDistances);
	}
	LOGINFO("Objects allocated (initial pool size was %i) %u", startingSize + blockSize, allocated);
	LOGINFO("Memory overhead %u B", (allocated - startingSize) * sizeof(uint64_t));

	CADT_VectorDestroy(allocTracker);
	Handle_DynamicManager32Destroy(manager);
}

