> :warning: **DISCONTINUATION OF PROJECT** - 
> *This project will no longer be maintained by Intel.
> Intel has ceased development and contributions including, but not limited to, maintenance, bug fixes, new releases, or updates, to this project.*
> **Intel no longer accepts patches to this project.**
> *If you have an ongoing need to use this project, are interested in independently developing it, or would like to maintain patches for the open source software community, please create your own fork of this project.*


Cyclone is a building block for highly available and concurrent applications
that manipulate persistent memory.

Cyclone uses RAFT for distributed consensus on a log of operations. Cyclone also
depends on Intel's pmem library (pmem.io) and persists the log and RAFT state
such as term and voted for using the pmem library.

Dependencies:

1. Install raft. https://github.com/willemt/raft Currently known working commit
is 8fb1f6c0402b4b7db2aa61e4bf8d525a76f15986


2. Install zmq. zeromq.org Currently known working version 4.1.3

3. Install pmem library from https://github.com/pmem/nvml/releases Knnown to
work with 0.3


