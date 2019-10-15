cmake_minimum_required(VERSION 3.12.4)
get_directory_property(hasParent PARENT_DIRECTORY)
if(NOT hasParent)
	option(unittests "unittests" OFF)
	get_filename_component(_PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
	set_property(GLOBAL PROPERTY GLOBAL_FETCHDEPS_BASE ${_PARENT_DIR}/al2o3 )
	include(FetchContent)
	FetchContent_Declare( al2o3 GIT_REPOSITORY https://github.com/DeanoC/al2o3 GIT_TAG master )
	FetchContent_GetProperties(al2o3)
	if(NOT al2o3_POPULATED)
		FetchContent_Populate(al2o3)
		add_subdirectory(${al2o3_SOURCE_DIR} ${al2o3_BINARY_DIR})
	endif()
	INIT_AL2O3(${CMAKE_CURRENT_SOURCE_DIR})
endif ()

SET_MIN_VERSIONS()
set(LibName al2o3_handle)
project(${LibName})

file(GLOB_RECURSE Src CONFIGURE_DEPENDS include/*.h include/*.hpp src/*.c src/*.cpp)
set(Deps
    	al2o3_platform
		al2o3_memory
		al2o3_thread
		)

ADD_LIB2(${LibName} "${Src}" "${Deps}")

file( GLOB_RECURSE Tests CONFIGURE_DEPENDS tests/*.cpp )

set( TestDeps
		al2o3_catch2
		al2o3_cadt
		utils_simple_logmanager)
ADD_LIB2_TESTS(${LibName} "${Tests}" "${TestDeps}")

