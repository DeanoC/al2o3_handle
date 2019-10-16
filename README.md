![](https://github.com/DeanoC/al2o3_handle/Build/badge.svg)
![](https://github.com/DeanoC/al2o3_handle/Test/badge.svg)


# Handle Manager
A generational safe handle/pool system for access to objects safer than a raw pointer

Benfits:
32 bit handle can access 16.7 millions same sized objects.
Each object can be upto 4GB in size (in theory).

Handle uniquely represent a particular object and when it was allocated.
Using a handle to a particular object that has been released will (in most cases)
report and error.

Fixed or dynamic sizing pool. 

Implementation are thread safe.
Fixed is non locking, and a owned objects can be freely
accessed until released.
Dynamic has locking interface to ensure and access is safer.

'Virtual' implementation allows either the fixed or dynamic
variant to be used with the same code (except for construction)

Handles have generational safety helping to detect corruption.
Access after free, invalidation etc.

Handle mnaager actively extends the time between generational
overlap as best it can. When generational overlap occurs, no false reporting
only effect is that a invalid access might not be reported.

Low over memory head, beyond the manager header only 1 byte per object is used 
outside the objects themselves.

TODO: Dynamic implmentation needs redoing to stop pointer invalidation and
requiring locks during access to owned handle.