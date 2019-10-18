![](https://github.com/DeanoC/al2o3_handle/Build/badge.svg)
![](https://github.com/DeanoC/al2o3_handle/Test/badge.svg)


# Handle Manager

A generational safe handle/pool system for access to objects safer than a raw pointer

Benfits:

32 bit handle can access 16.7 millions same sized objects. Each object can be upto 4GB in size (in theory). Objects are allocated in blocks (number of objects in a blocks are a creation parameter). Time to allocate and release an object is low and linear.

Fixed or dynamic sizing pool. Both implementation are non-locking and thread safe.

A Handle uniquely represent a particular object and when it was allocated. Using a handle to a particular object that has been released will report and error (in most cases).

Handles have generational safety helping to detect corruption. Access after free, invalidation etc. will all be detected fairly well. Can optionally choose on dynamic to never reuse a handle.

Handle manager actively extends the time between generational overlap as best it can. When handle reuse is allowed, will take the longest number of alloc/release cycles it can (256 * handlers per block size).

When generational overlap occurs, no false reporting only effect is that an invalid access might not be reported. 

Low memory over head, beyond the manager header only 1 byte + object size is used.

Fixed sized allocator only has 16 byte header. Dynamic is bigger and depends on the maximum number of block allowed.

Pointer are never invalidated! Between an alloc and release, the memory and pointer to it are yours and will not change under you regardless and any other thread activity (unless another thread destroy the manager itself).
