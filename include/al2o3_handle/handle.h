#pragma once

typedef struct Handle_Manager32 *Handle_Manager32Handle;
typedef struct Handle_Manager64 *Handle_Manager64Handle;

// A 32 bit handle can access 16.7 million objects and 256 generations per handle
// Handle_InvalidHandle32 == 0 to help catch clear before alloc bugs
typedef uint32_t Handle_Handle32;

typedef uint64_t Handle_Handle64;

#define Handle_InvalidHandle32 0
