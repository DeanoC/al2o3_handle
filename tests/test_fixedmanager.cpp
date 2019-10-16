// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_thread/thread.h"
#include "al2o3_catch2/catch2.hpp"
#include "al2o3_handle/fixed.h"

#include "al2o3_cadt/vector.h"

#include <inttypes.h>

namespace {
struct Test {

	char data[256];
};

static void FillTest(Test* test) {
	for(int i=0;i < 256;++i) {
		test->data[i] = i;
	}
}
} // end anon namespace

TEST_CASE("Basic tests Fixed", "[al2o3 handle fixed]") {
	Handle_FixedManager32* manager = Handle_FixedManager32Create(sizeof(Test), 16);
	REQUIRE(manager);

	Handle_FixedHandle32 handle0 = Handle_FixedManager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_FixedManager32Release(manager, handle0);
	Handle_FixedHandle32 handle1 = Handle_FixedManager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_FixedManager32Release(manager, handle1);

	Handle_FixedManager32Destroy(manager);
}

TEST_CASE("generation tests Fixed", "[al2o3 handle fixed]") {
	static const int AllocationBlockSize = 16;
	Handle_FixedManager32* manager = Handle_FixedManager32Create(sizeof(Test), AllocationBlockSize*4);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_FixedHandle32 handle = Handle_FixedManager32Alloc(manager);
		Handle_FixedManager32Release(manager, handle);
		REQUIRE(Handle_FixedManager32IsValid(manager, handle) == false);
	}

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_FixedHandle32 handle = Handle_FixedManager32Alloc(manager);
		REQUIRE(Handle_FixedManager32IsValid(manager, handle) == true);
		Handle_FixedManager32Release(manager, handle);
	}

	Handle_FixedManager32Destroy(manager);
}
TEST_CASE("Run out of handles test Fixed", "[al2o3 handle fixed]") {
	static const int AllocationBlockSize = 16;
	Handle_FixedManager32* manager = Handle_FixedManager32Create(sizeof(Test), AllocationBlockSize);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize; ++i) {
		Handle_FixedHandle32 handle = Handle_FixedManager32Alloc(manager);
	}
	// there will be a warning log telling us we have run out but it will return invalid handle
	LOGINFO("The next WARN is expected as we are testing the invalid value is return");
	REQUIRE( Handle_FixedManager32Alloc(manager) == Handle_InvalidFixedHandle32);

}

TEST_CASE("data access tests Fixed", "[al2o3 handle fixed]") {
	static const int AllocationBlockSize = 16;
	Handle_FixedManager32* manager = Handle_FixedManager32Create(sizeof(Test), AllocationBlockSize*4);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_FixedHandle32 dataHandle = Handle_FixedManager32Alloc(manager);
	void * unsafePtr = Handle_FixedManager32HandleToPtr(manager, dataHandle);
	// alloc always returns zero'ed data
	REQUIRE(memcmp(unsafePtr, &zeroData, sizeof(Test)) == 0);
	memcpy(unsafePtr, &testData, sizeof(Test));

	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);
	Handle_FixedManager32Release(manager, dataHandle);
	REQUIRE(Handle_FixedManager32IsValid(manager, dataHandle) == false);
	// release doesn't zero the data but it will be different
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) != 0);

	Handle_FixedManager32Destroy(manager);
}

static Thread_Atomic64_t leaked = {0};
static void InternalThreadFunc(Handle_FixedManager32* manager, uint64_t totalAllocReleaseCycles ) {

	uint64_t allocReleaseCycles = 0;

	while(allocReleaseCycles != totalAllocReleaseCycles ) {

		Handle_FixedHandle32 handle = Handle_FixedManager32Alloc(manager);
		if(handle == Handle_InvalidFixedHandle32) {
			return;
		}

		*(uint64_t *)Handle_FixedManager32HandleToPtr(manager, handle) = allocReleaseCycles;

		for (auto i = 0u; i < 100; ++i) {
			uint64_t * test = (uint64_t*)Handle_FixedManager32HandleToPtr(manager, handle);
			if(*test != allocReleaseCycles) {
				LOGINFO("%" PRId64 "  %" PRId64, allocReleaseCycles, *test);
			}
		}
		// every now and again don't release the handle to mix things up
		if((allocReleaseCycles % 1000) != 0) {
			Handle_FixedManager32Release(manager, handle);
		} else {
			Thread_AtomicFetchAdd64Relaxed(&leaked, 1);
		}
		allocReleaseCycles++;
	}
}

#if NDEBUG
static const uint64_t totalAllocReleaseCycles = 10000000ull;
#else
static const uint64_t totalAllocReleaseCycles = 1000000ull;
#endif


static void ThreadFuncFixed(void* userPtr) {
	InternalThreadFunc((Handle_FixedManager32*) userPtr, totalAllocReleaseCycles);
}

TEST_CASE("Multithreaded FixedManager", "[al2o3 handle fixed]") {

	LOGINFO("---------------------------------------------------------------------");
	LOGINFO("Starting multithread stress fixed handle manager test - takes a while");

	static const uint32_t numThreads = Thread_CPUCoreCount() * 5;
	static const uint64_t totalTotalAllocReleaseCycles = totalAllocReleaseCycles * numThreads;
	static const uint32_t totalSize = (totalTotalAllocReleaseCycles/ 1000) + (numThreads*2);
	Handle_FixedManager32* manager = Handle_FixedManager32Create(sizeof(uint64_t), totalSize);
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
	LOGINFO("After %" PRId64 " million alloc/release cycles", totalTotalAllocReleaseCycles / 1000000ull);

	LOGINFO("Delibrately Leaked %" PRId64 " Objects, expected leak count %" PRId64,
					leakedCount,
					totalTotalAllocReleaseCycles / 1000);
	LOGINFO("Total handles allocated (including leaks) %u", totalSize);

	Handle_FixedManager32Destroy(manager);
}


TEST_CASE("Generation overflow stats Fixed", "[al2o3 handle fixed]") {

	// distance should be in the order of 256 * totalsize
	// with totalSize = 1K, and 1 billion alloc/release, distance = 261821
	static const uint32_t totalSize = 1024 * 1;
	static const uint64_t totalTotalAllocReleaseCycles = (totalAllocReleaseCycles/10) * totalSize;
	Handle_FixedManager32* manager = Handle_FixedManager32Create(sizeof(uint64_t), totalSize);

	LOGINFO("---------------------------------------------------------------------");
	LOGINFO("Starting generation overflow fixed handle manager test - takes a while");

	REQUIRE(manager);

	uint64_t allocReleaseCycles = 0;
	uint64_t distance = 0;
	uint64_t numDistances = 0;
	CADT_VectorHandle allocTracker = CADT_VectorCreate(sizeof(uint64_t));
	CADT_VectorReserve(allocTracker, totalSize);

	Handle_FixedManager32Alloc(manager); // remove 0 index from this test
	CADT_VectorPushElement(allocTracker, &allocReleaseCycles);

	CADT_VectorResize(allocTracker, totalSize);
	uint64_t* allocTrackerMem = (uint64_t*)CADT_VectorData(allocTracker);
	memset(allocTrackerMem, 0, CADT_VectorSize(allocTracker) * sizeof(CADT_VectorElementSize(allocTracker)));

	while(allocReleaseCycles != totalTotalAllocReleaseCycles ) {
		Handle_FixedHandle32 handle = Handle_FixedManager32Alloc(manager);
		// we use 1st gen due to the special case of handle 0
		if((handle >> 24u) == 1) {
			uint32_t const index = (handle & 0x00FFFFFFu);
			if(*(allocTrackerMem + index) != 0) {
				distance += allocReleaseCycles - *(allocTrackerMem + index);
				numDistances++;
			}
			*(allocTrackerMem + index) = allocReleaseCycles;
		}
		Handle_FixedManager32Release(manager, handle);
		allocReleaseCycles++;
	}

	LOGINFO("After %" PRId64 " million alloc/release cycles", totalTotalAllocReleaseCycles / 1000000ull);
	if(numDistances == 0) {
		LOGINFO("No handle generation overflow has occured");
	} else {
		LOGINFO("Average distance between handle generation reuse %i", distance / numDistances);
	}
	LOGINFO("Objects allocated %u", totalSize);

	CADT_VectorDestroy(allocTracker);
	Handle_FixedManager32Destroy(manager);
}
