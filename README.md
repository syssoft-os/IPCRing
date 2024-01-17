# IPCRing
Implementing a logical ring of processes using different types of IPC mechanisms

## How to build
Should build on **Windows** and **POSIX** systems. But most system calls raise ``ENOSYS`` on Windows.\
To build on **POSIX** systems just run ``make``.\
To build on **Windows** use your preferred way of building.

## How to use
### fork_ring:
Run ``./fork_ring <count>``. Where ``<count>`` is the number of child processes to spawn.\
On **Windows** only ``<count>`` 0 is supported.

### mixed_ipc:
Run ``./mixed_ipc -it (np|shm|so|zmq) [-in <in_name>] -ot (np|shm|so|zmq) [-on <out_name>] [-s] [-k [<stop_value>]]``.
- ``-it`` (required): next argument is the type of input to use.
- ``-ot`` (required): next argument is the type of output to use.
- Supported input and output types are ``np`` (named pipe), ``shm`` (shared memory), ``so`` (socket) and ``zmq`` (ZeroMQ).
- ``-in`` (optional): next argument is the name for the input IPC mechanism to connect to.
- ``-on`` (optional): next argument is the name for the output IPC mechanism to connect to.
- If ``-in`` or ``-on`` are not specified the program will create input or output IPC mechanisms of types specified by ``-it`` and ``-ot`` respectively. The generated IPC mechanisms will have auto generated names, that are logged to the console.
- ``-s`` (optional): Indicates that the program should start sending the first packet.
- ``-k`` (optional): Indicates that the program should propagate a kill signal upon closing.
If ``<stop_value>`` is specified the program terminates after the counter has reached the given number.

Note that IPC mechanisms like Signals and Message Queues are not supported,
because **WSL** was used for testing, wich currently does not implement them.\
On **Windows** only Sockets are supported.
