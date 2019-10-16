// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_catch2/catch2.hpp"
#include "al2o3_handle/handle.h"
#include "al2o3_handle/dynamic.h"
#include "al2o3_handle/fixed.h"

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
TEST_CASE("Basic tests Virtual Dynamic", "[al2o3 handle]") {
	Handle_DynamicManager32* dmanager = Handle_ManagerDynamic32Create(sizeof(Test), 16, 16);
	REQUIRE(dmanager);
	Handle_Manager32* manager = Handle_Manager32CreateFromDynamic(dmanager);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}


TEST_CASE("Block allocation tests Virtual Dynamic", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_DynamicManager32* dmanager =
			Handle_ManagerDynamic32Create(sizeof(Test), AllocationBlockSize, AllocationBlockSize);
	REQUIRE(dmanager);
	Handle_Manager32* manager = Handle_Manager32CreateFromDynamic(dmanager);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_DynamicHandle32 handle = Handle_Manager32Alloc(manager);
		if( i == 0) {
			REQUIRE(handle == 0x01000000);
		} else {
			REQUIRE(handle == i);
		}
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("Basic tests Virtual Fixed", "[al2o3 handle]") {
	Handle_FixedManager32* fmanager = Handle_FixedManager32Create(sizeof(Test), 16);
	REQUIRE(fmanager);
	Handle_Manager32* manager = Handle_Manager32CreateFromFixed(fmanager);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0 == 0x01000000);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1 == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}


TEST_CASE("Block allocation tests Virtual Fixed", "[al2o3 handle]") {
	Handle_FixedManager32* fmanager = Handle_FixedManager32Create(sizeof(Test), 16);
	REQUIRE(fmanager);
	Handle_Manager32* manager = Handle_Manager32CreateFromFixed(fmanager);
	REQUIRE(manager);

	for(int i =0 ; i < 16;++i) {
		Handle_DynamicHandle32 handle = Handle_Manager32Alloc(manager);
		if( i == 0) {
			REQUIRE(handle == 0x01000000);
		} else {
			REQUIRE(handle == i);
		}
	}

	Handle_Manager32Destroy(manager);
}

static Thread_Atomic64_t leaked = {0};
static void InternalThreadFunc(Handle_Manager32* manager, uint64_t totalAllocReleaseCycles ) {

	uint64_t allocReleaseCycles = 0;

	while(allocReleaseCycles != totalAllocReleaseCycles ) {

		Handle_DynamicHandle32 handle = Handle_Manager32Alloc(manager);
		if(handle == Handle_InvalidHandle32) {
			return;
		}

		Handle_Manager32CopyFromMemory(manager, handle, &allocReleaseCycles);
		uint64_t test;
		for (auto i = 0u; i < 100; ++i) {
			Handle_Manager32CopyToMemory(manager, handle, &test);
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

#if NDEBUG
static const uint64_t totalAllocReleaseCycles = 1000000ull;
#else
static const uint64_t totalAllocReleaseCycles = 100000ull;
#endif

static void ThreadFuncDynamic(void* userPtr) {

	Handle_Manager32* manager = (Handle_Manager32*) userPtr;
	InternalThreadFunc(manager, totalAllocReleaseCycles);
}

TEST_CASE(" Multithreaded Virtual Dynamic", "[al2o3 handle]") {

	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting multithread stress virtual dynamic handle manager test - takes a while");
	const uint32_t startingSize = 1024 * 1;
	const uint32_t blockSize = 16;
	const uint32_t numThreads = Thread_CPUCoreCount() * 5;
	const uint64_t totalTotalAllocReleaseCycles = totalAllocReleaseCycles * numThreads;
	const uint32_t totalSize = (totalTotalAllocReleaseCycles/ 1000) + (numThreads*2);

	Handle_DynamicManager32* dmanager = Handle_ManagerDynamic32Create(sizeof(uint64_t), startingSize, blockSize);
	REQUIRE(dmanager);
	Handle_Manager32* manager= Handle_Manager32CreateFromDynamic(dmanager);

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
	LOGINFO("Total handles allocated (including leaks) %u", Handle_Manager32HandleAllocatedCount(manager));
	Handle_Manager32Destroy(manager);
}

TEST_CASE(" Multithreaded Virtual Fixed", "[al2o3 handle]") {

	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting multithread stress virtual fixed handle manager test - takes a while");
	const uint32_t numThreads = Thread_CPUCoreCount() * 5;
	static const uint64_t totalTotalAllocReleaseCycles = totalAllocReleaseCycles * numThreads;
	static const uint32_t totalSize = (totalTotalAllocReleaseCycles/ 1000) + (numThreads*2);
	Handle_FixedManager32* fmanager = Handle_FixedManager32Create(sizeof(uint64_t), totalSize);
	Handle_Manager32* manager= Handle_Manager32CreateFromFixed(fmanager);

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
	LOGINFO("Total handles allocated (including leaks) %u", Handle_Manager32HandleAllocatedCount(manager));
	Handle_Manager32Destroy(manager);
}
