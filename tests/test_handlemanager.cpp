// License Summary: MIT see LICENSE file
#include "al2o3_platform/platform.h"
#include "al2o3_thread/atomic.h"
#include "al2o3_catch2/catch2.hpp"
#include "al2o3_handle/handle.h"
#include "al2o3_cadt/vector.h"
#include "al2o3_thread/thread.h"
#include "utils_simple_logmanager/logmanager.h"

#include <inttypes.h>

//we use this to quiet some warnings that we expect during test
extern SimpleLogManager_Handle logger;

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
TEST_CASE("Basic tests 32", "[al2o3 handle]") {
	Handle_Manager32* manager = Handle_Manager32Create(sizeof(Test), 16, 1, false);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0.handle == 0x01000000);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1.handle == 1);
	Handle_Manager32Release(manager, handle1);

	Handle_Manager32Destroy(manager);
}


TEST_CASE("Block allocation tests 32", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32* manager = Handle_Manager32Create(sizeof(Test),
			AllocationBlockSize,
			4,
			false);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if( i == 0) {
			REQUIRE(handle.handle == 0x01000000);
		} else {
			REQUIRE(handle.handle == i);
		}
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("generation tests 32", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32* manager =
			Handle_Manager32Create(sizeof(Test), AllocationBlockSize, 4, false);
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

TEST_CASE("data access tests 32", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager32* manager = Handle_Manager32Create(sizeof(Test), AllocationBlockSize, 4, false);
	REQUIRE(manager);

	Test zeroData;
	memset(&zeroData, 0, sizeof(Test));
	Test testData;
	FillTest(&testData);

	Handle_Handle32 dataHandle = Handle_Manager32Alloc(manager);
	void * unsafePtr = Handle_Manager32HandleToPtr(manager, dataHandle);
	// alloc always returns zero'ed data
	REQUIRE(memcmp(unsafePtr, &zeroData, sizeof(Test)) == 0);
	memcpy(unsafePtr, &testData, sizeof(Test));

	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) == 0);
	Handle_Manager32Release(manager, dataHandle);
	REQUIRE(Handle_Manager32IsValid(manager, dataHandle) == false);
	// release doesn't zero the data but it will be different
	REQUIRE(memcmp(unsafePtr, &testData, sizeof(Test)) != 0);

	// now force a internal realloc and check the data is okay
	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Manager32Alloc(manager);
	}

	Handle_Manager32Destroy(manager);
}

TEST_CASE("clone 32", "[al2o3 handle]") {
	Handle_Manager32* manager = Handle_Manager32Create(sizeof(Test), 16, 1, false);
	REQUIRE(manager);

	Handle_Handle32 handle0 = Handle_Manager32Alloc(manager);
	REQUIRE(handle0.handle == 0x01000000);
	Handle_Manager32Release(manager, handle0);
	Handle_Handle32 handle1 = Handle_Manager32Alloc(manager);
	REQUIRE(handle1.handle == 1);

	Handle_Manager32* clone = Handle_Manager32Clone(manager);
	REQUIRE(!Handle_Manager32IsValid(manager, handle0));
	REQUIRE(!Handle_Manager32IsValid(clone, handle0));
	REQUIRE(Handle_Manager32IsValid(manager, handle1));
	REQUIRE(Handle_Manager32IsValid(clone, handle1));

	Handle_Manager32Destroy(clone);
	Handle_Manager32Destroy(manager);
}


TEST_CASE("Basic tests 64", "[al2o3 handle]") {
	Handle_Manager64* manager = Handle_Manager64Create(sizeof(Test), 16, 1, false);
	REQUIRE(manager);

	Handle_Handle64 handle0 = Handle_Manager64Alloc(manager);
	REQUIRE(handle0.handle == 0x10000000000ull);
	Handle_Manager64Release(manager, handle0);
	Handle_Handle64 handle1 = Handle_Manager64Alloc(manager);
	REQUIRE(handle1.handle == 1);
	Handle_Manager64Release(manager, handle1);

	Handle_Manager64Destroy(manager);
}


TEST_CASE("Block allocation tests 64", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager64* manager = Handle_Manager64Create(sizeof(Test),
																										 AllocationBlockSize,
																										 4,
																										 false);
	REQUIRE(manager);

	for(int i =0 ; i < AllocationBlockSize * 4;++i) {
		Handle_Handle64 handle = Handle_Manager64Alloc(manager);
		if( i == 0) {
			REQUIRE(handle.handle == 0x10000000000ull);
		} else {
			REQUIRE(handle.handle == i);
		}
	}

	Handle_Manager64Destroy(manager);
}

TEST_CASE("clone 64", "[al2o3 handle]") {
	Handle_Manager64* manager = Handle_Manager64Create(sizeof(Test), 16, 1, false);
	REQUIRE(manager);

	Handle_Handle64 handle0 = Handle_Manager64Alloc(manager);
	REQUIRE(handle0.handle == 0x10000000000ull);
	Handle_Manager64Release(manager, handle0);
	Handle_Handle64 handle1 = Handle_Manager64Alloc(manager);
	REQUIRE(handle1.handle == 1);

	Handle_Manager64* clone = Handle_Manager64Clone(manager);
	REQUIRE(!Handle_Manager64IsValid(manager, handle0));
	REQUIRE(!Handle_Manager64IsValid(clone, handle0));
	REQUIRE(Handle_Manager64IsValid(manager, handle1));
	REQUIRE(Handle_Manager64IsValid(clone, handle1));

	Handle_Manager64Destroy(clone);
	Handle_Manager64Destroy(manager);
}


TEST_CASE("generation tests 64", "[al2o3 handle]") {
	static const int AllocationBlockSize = 16;
	Handle_Manager64* manager =
			Handle_Manager64Create(sizeof(Test), AllocationBlockSize, 4, false);
	REQUIRE(manager);

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Handle64 handle = Handle_Manager64Alloc(manager);
		Handle_Manager64Release(manager, handle);
		REQUIRE(Handle_Manager64IsValid(manager, handle) == false);
	}

	for (int i = 0; i < AllocationBlockSize * 4; ++i) {
		Handle_Handle64 handle = Handle_Manager64Alloc(manager);
		REQUIRE(Handle_Manager64IsValid(manager, handle) == true);
		Handle_Manager64Release(manager, handle);
	}

	Handle_Manager64Destroy(manager);
}



//------------------ Advanced tests -------------------//

static Thread_Atomic64_t leaked = {0};
static void InternalThreadFunc32(Handle_Manager32* manager, uint64_t totalAllocReleaseCycles ) {

	uint64_t allocReleaseCycles = 0;

	while(allocReleaseCycles != totalAllocReleaseCycles ) {

		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if(!Handle_Manager32IsValid(manager, handle)) {
			LOGWARNING("Invalid Handle");
			return;
		}

		*(uint64_t *)Handle_Manager32HandleToPtr(manager, handle) = allocReleaseCycles;

		for (auto i = 0u; i < 100; ++i) {
			uint64_t * test = (uint64_t*)Handle_Manager32HandleToPtr(manager, handle);
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
			Handle_Manager32Release(manager, handle);
		} else {
			Thread_AtomicFetchAdd64Relaxed(&leaked, 1);
		}
		allocReleaseCycles++;
	}
}


static const uint64_t totalAllocReleaseCycles = 5000000ull;

static void ThreadFunc32(void* userPtr) {

	Handle_Manager32* manager = (Handle_Manager32*) userPtr;
	InternalThreadFunc32(manager, totalAllocReleaseCycles);
}
void Multithread32Loop(const uint32_t blockSize, const bool expectedResult) {
	const uint32_t numThreads = 20;
	const uint64_t totalTotalAllocReleaseCycles = totalAllocReleaseCycles * numThreads;

	Handle_Manager32* manager = Handle_Manager32Create(sizeof(uint64_t),
			blockSize,
			256,
			false);
	REQUIRE(manager);

	Thread_AtomicStore64Relaxed(&leaked, 0);

	Thread_Thread * threads = (Thread_Thread *)STACK_ALLOC(sizeof(Thread_Thread) * numThreads);

	for (auto i = 0u; i < numThreads; ++i) {
		Thread_ThreadCreate(threads + i, &ThreadFunc32, manager);
	}

	for (auto i = 0u; i < numThreads; ++i) {
		Thread_ThreadJoin(threads + i);
		Thread_ThreadDestroy(threads + i);
	}
	uint64_t leakedCount = Thread_AtomicLoad64Relaxed(&leaked);
	LOGINFO("After trying %" PRId64 " million alloc/release cycles", totalTotalAllocReleaseCycles / 1000000ull);

	LOGINFO("Delibrately Leaked %" PRId64 " Objects, expected leak count %" PRId64,
					leakedCount,
					totalTotalAllocReleaseCycles / 1000);
	REQUIRE(((leakedCount - totalTotalAllocReleaseCycles / 1000) == 0) == expectedResult);
	Handle_Manager32Destroy(manager);

}
TEST_CASE(" Multithreaded 32", "[al2o3 handle]") {
	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting multithread dynamic handle manager stress test - takes a while");
	uint32_t blockSize = 2;

	while(blockSize < 65536) {
		LOGINFO("-----------------------------");
		LOGINFO("BlockSize: %u", blockSize);
		int64_t const maxHandles = (blockSize * 256);
		int64_t const leaksExpected = ((totalAllocReleaseCycles / 1000)*20);
		bool const expected =  leaksExpected <= maxHandles;
		SimpleLogManager_SetWarningQuiet(logger, true);
		Multithread32Loop(blockSize, expected);
		if(expected == false) {
			LOGINFO("Blocksize was too small, handled handle exhaustion correctly");
		}
		SimpleLogManager_SetWarningQuiet(logger, false);
		blockSize *= 2;
	}
}

TEST_CASE("Generation overflow stats 32", "[al2o3 handle]") {
	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting generation overflow 32bit handle manager test - takes a while");

	static const uint32_t blockSize = 16;
	static const uint64_t totalAllocReleaseCycles = 10000000ull;

	Handle_Manager32* manager = Handle_Manager32Create(sizeof(Test),
			blockSize,
			256,
			false);
	REQUIRE(manager);

	uint64_t allocReleaseCycles = 0;
	uint64_t distance = 0;
	uint64_t numDistances = 0;
	CADT_VectorHandle allocTracker = CADT_VectorCreate(sizeof(uint64_t));
	for (uint64_t i = 0u; i < blockSize; ++i) {
		CADT_VectorPushElement(allocTracker, &i);
	}

	while(allocReleaseCycles != totalAllocReleaseCycles ) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		if((handle.handle >> Handle_GenerationBitShift32) == 0) {
			uint32_t const index = (handle.handle & Handle_MaxHandles32);
			distance += allocReleaseCycles - *((uint64_t*)CADT_VectorAt(allocTracker, index));
			numDistances++;
			*(uint64_t*)CADT_VectorAt(allocTracker,index) = allocReleaseCycles;
		}
		Handle_Manager32Release(manager, handle);
		allocReleaseCycles++;
	}

	LOGINFO("After %" PRId64 " million alloc/release cycles", totalAllocReleaseCycles / 1000000ull);
	uint32_t allocated = Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated);
	if(numDistances == 0) {
		LOGINFO("No handle generation overflow has occured");
	} else {
		LOGINFO("Average distance between handle generation reuse %i", distance / numDistances);
		LOGINFO("Expected Distance %u", (blockSize * (1<<8)));
	}
	LOGINFO("Objects allocated %u", allocated);

	CADT_VectorDestroy(allocTracker);
	Handle_Manager32Destroy(manager);
}


TEST_CASE("Generation overflow stats 32 never reissue old handles", "[al2o3 handle]") {
	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting generation overflow 32 bit handle manager with never reissue test - takes a while");

	static const uint32_t blockSize = 1024 * 16;
	static const uint64_t totalAllocReleaseCycles = 100000000ull;
	Handle_Manager32* manager = Handle_Manager32Create(sizeof(Test),
			blockSize,
			256,
			true);
	REQUIRE(manager);

	uint64_t allocReleaseCycles = 0;
	uint64_t gen0Max = 0;

	Handle_Manager32Alloc(manager); // remove 0 index from this test

	while(allocReleaseCycles != totalAllocReleaseCycles ) {
		Handle_Handle32 handle = Handle_Manager32Alloc(manager);
		REQUIRE(Handle_Manager32IsValid(manager, handle));
		if((handle.handle >> Handle_GenerationSize32) == 0) {
			// gen 0 should only ever increase in index (no reuse)
			uint32_t const index = (handle.handle & Handle_GenerationSize32);
			REQUIRE(index > gen0Max);
			gen0Max = index;
		}
		Handle_Manager32Release(manager, handle);
		allocReleaseCycles++;
	}

	LOGINFO("After %" PRId64 " million alloc/release cycles", totalAllocReleaseCycles / 1000000ull);
	uint32_t allocated = Thread_AtomicLoad32Relaxed(&manager->totalHandlesAllocated);
	LOGINFO("Object overhead by never reuse: %u", allocated - blockSize);
	LOGINFO("Memory overhead (with 8 byte objects): %u B", (allocated - blockSize) * sizeof(uint64_t));

	Handle_Manager32Destroy(manager);
}


TEST_CASE("Generation overflow stats 64", "[al2o3 handle]") {
	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting generation overflow 64bit handle manager test - takes a while");

	static const uint32_t blockSize = 16;
	static const uint64_t totalAllocReleaseCycles = 1000000000ull;

	Handle_Manager64* manager = Handle_Manager64Create(sizeof(Test),
																										 blockSize,
																										 256,
																										 false);
	REQUIRE(manager);

	uint64_t allocReleaseCycles = 0;
	uint64_t distance = 0;
	uint64_t numDistances = 0;
	CADT_VectorHandle allocTracker = CADT_VectorCreate(sizeof(uint64_t));
	for (uint64_t  i = 0u; i < blockSize; ++i) {
		CADT_VectorPushElement(allocTracker, &i);
	}

	while(allocReleaseCycles != totalAllocReleaseCycles ) {
		Handle_Handle64 handle = Handle_Manager64Alloc(manager);
		if((handle.handle >> Handle_GenerationBitShift64) == 1) {
			uint64_t const index = (handle.handle & Handle_MaxHandles64);
			distance += allocReleaseCycles - *((uint64_t*)CADT_VectorAt(allocTracker, index));
			numDistances++;
			*(uint64_t*)CADT_VectorAt(allocTracker,index) = allocReleaseCycles;
		}
		Handle_Manager64Release(manager, handle);
		allocReleaseCycles++;
	}

	LOGINFO("After %" PRId64 " million alloc/release cycles", totalAllocReleaseCycles / 1000000ull);
	uint64_t allocated = Thread_AtomicLoad64Relaxed(&manager->totalHandlesAllocated);
	if(numDistances == 0) {
		LOGINFO("No handle generation overflow has occured");
	} else {
		LOGINFO("Average distance between handle generation reuse %i", distance / numDistances);
		LOGINFO("Expected Distance %u", (blockSize * (1<<24)));
	}
	LOGINFO("Objects allocated %u", allocated);

	CADT_VectorDestroy(allocTracker);
	Handle_Manager64Destroy(manager);
}

TEST_CASE("Generation overflow stats 64 never reissue old handles", "[al2o3 handle]") {
	LOGINFO("-----------------------------------------------------------------------");
	LOGINFO("Starting generation overflow 64 bit handle manager with never reissue test - takes a while");

	static const uint32_t blockSize = 16;
	static const uint64_t totalAllocReleaseCycles = 400000000ull;
	Handle_Manager64* manager = Handle_Manager64Create(sizeof(Test),
																										 blockSize,
																										 256,
																										 true);
	REQUIRE(manager);

	uint64_t allocReleaseCycles = 0;
	uint64_t gen0Max = 0;

	Handle_Manager64Alloc(manager); // remove 0 index from this test

	while(allocReleaseCycles != totalAllocReleaseCycles ) {
		Handle_Handle64 handle = Handle_Manager64Alloc(manager);
		REQUIRE(Handle_Manager64IsValid(manager, handle));
		if((handle.handle >> Handle_GenerationSize64) == 0) {
			// gen 0 should only ever increase in index (no reuse)
			uint64_t const index = (handle.handle & Handle_MaxHandles64);
			REQUIRE(index > gen0Max);
			gen0Max = index;
		}
		Handle_Manager64Release(manager, handle);
		allocReleaseCycles++;
	}

	LOGINFO("After %" PRId64 " million alloc/release cycles", totalAllocReleaseCycles / 1000000ull);
	uint64_t allocated = Thread_AtomicLoad64Relaxed(&manager->totalHandlesAllocated);
	LOGINFO("Object overhead by never reuse: %u", allocated - blockSize);
	LOGINFO("Memory overhead (with 8 byte objects): %u B", (allocated - blockSize) * sizeof(uint64_t));

	Handle_Manager64Destroy(manager);
}
