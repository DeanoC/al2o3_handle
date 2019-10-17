// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_catch2/catch2.hpp"
#include "al2o3_handle/dynamic.h"
#include "al2o3_cadt/vector.h"
#include "al2o3_thread/thread.h"

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
} // end anon namespace
TEST_CASE("Basic tests Dynamic", "[al2o3 handle]") {
	Handle_DynamicManager32* manager = Handle_DynamicManager32Create(sizeof(Test), 16);
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
	Handle_DynamicManager32* manager = Handle_DynamicManager32Create(sizeof(Test), AllocationBlockSize);
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

TEST_CASE("generation tests Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* manager =
			Handle_DynamicManager32Create(sizeof(Test), AllocationBlockSize);
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
			Handle_DynamicManager32Create(sizeof(Test), AllocationBlockSize);
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

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_DynamicManager32Alloc(manager);
	}

	Handle_DynamicManager32Destroy(manager);
}

static Thread_Atomic64_t leaked = {0};
static void InternalThreadFunc(Handle_DynamicManager32* manager, uint64_t totalAllocReleaseCycles ) {

	uint64_t allocReleaseCycles = 0;

	while(allocReleaseCycles != totalAllocReleaseCycles ) {

		Handle_DynamicHandle32 handle = Handle_DynamicManager32Alloc(manager);
		if(handle == Handle_InvalidDynamicHandle32) {
			LOGINFO("Invalid Handle");
			return;
		}

		*(uint64_t *)Handle_DynamicManager32HandleToPtr(manager, handle) = allocReleaseCycles;

		for (auto i = 0u; i < 100; ++i) {
			uint64_t * test = (uint64_t*)Handle_DynamicManager32HandleToPtr(manager, handle);
			if(test == nullptr) {
				LOGINFO("Handle_DynamicManager32HandleToPtr return NULL!");
				return;
			}
			if(*test != allocReleaseCycles) {
				LOGINFO("Total handles allocated %u", Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated));
				LOGINFO("%" PRId64 "  %" PRId64, allocReleaseCycles, (uintptr_t)test);
				return;
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

static const uint64_t totalAllocReleaseCycles = 100000000ull;

static void ThreadFuncDynamic(void* userPtr) {

	Handle_DynamicManager32* manager = (Handle_DynamicManager32*) userPtr;
	InternalThreadFunc(manager, totalAllocReleaseCycles);
}

TEST_CASE(" Multithreaded Dynamic", "[al2o3 handle]") {

	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting multithread dynamic handle manager stress test - takes a while");
	const uint32_t blockSize = 1024;
	const uint32_t numThreads = Thread_CPUCoreCount() * 5;
	const uint64_t totalTotalAllocReleaseCycles = totalAllocReleaseCycles * numThreads;

	Handle_DynamicManager32* manager = Handle_DynamicManager32Create(sizeof(uint64_t), blockSize);
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
	LOGINFO("Total handles allocated (including leaks) %u", Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated));
	Handle_DynamicManager32Destroy(manager);
}

TEST_CASE("Generation overflow stats Dynamic", "[al2o3 handle]") {
	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting generation overflow dynamic handle manager test - takes a while");

	static const uint32_t blockSize = 1024;
#if NDEBUG
	static const uint64_t totalAllocReleaseCycles = 1000000000ull;
#else
	static const uint64_t totalAllocReleaseCycles = 100000000ull;
#endif
	Handle_DynamicManager32* manager = Handle_DynamicManager32Create(sizeof(Test), blockSize);
	REQUIRE(manager);

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
	uint32_t allocated = Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated);
	if(numDistances == 0) {
		LOGINFO("No handle generation overflow has occured");
	} else {
		LOGINFO("Average distance between handle generation reuse %i", distance / numDistances);
	}
	LOGINFO("Objects allocated %u", allocated);
//	LOGINFO("Memory overhead %u B", (allocated - startingSize) * sizeof(uint64_t));

	CADT_VectorDestroy(allocTracker);
	Handle_DynamicManager32Destroy(manager);
}


