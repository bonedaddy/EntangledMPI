# Fault Tolerance Framework for High Performance Computing

[![Build Status](https://travis-ci.org/upperwal/EntangledMPI.svg?branch=master)](https://travis-ci.org/upperwal/EntangledMPI)

This project is a fault tolerance framework for parallel applications. Below is a list of supported features by this framework.

**This framework is still in development phase and not production ready. So please use it carefully.**

| #        | Features           							| Status  		|
| -------- |-------------         							| -----			|
| 1		   | ULFM Enabled									| master (OK)	|
| 2        | Process Replication 							| master (OK) 	|
| 3        | Full-context Application Level Checkpointing  	| master (OK) 	|
| 4		   | Fault Injector									| wip 			|
| 5	 	   | Process Manager 								| wip 			|
| 6        | User Level Checkpointing      					| future 		|

## MPI Functions Supported

| #        	| MPI Function           						| Status  		|
| -------- 	|-------------         							| -----			|
| 1 		| ```MPI_Init``` 								| master (OK) 	|
| 2 		| ```MPI_Comm_rank``` 							| master (OK) 	|
| 3 		| ```MPI_Comm_size``` 							| master (OK) 	|
| 4 		| ```MPI_Send``` 								| master (OK) 	|
| 5 		| ```MPI_Recv``` 								| master (OK) 	|
| 6 		| ```MPI_Scatter``` 							| master (OK) 	|
| 7 		| ```MPI_Gather``` 								| master (OK) 	|
| 8 		| ```MPI_Bcast``` 								| master (OK) 	|
| 9 		| ```MPI_Allgather``` 							| master (OK) 	|
| 10 		| ```MPI_Reduce``` 								| master (OK) 	|
| 11 		| ```MPI_Allreduce``` 							| master (OK) 	|
| 12 		| ```MPI_*``` (Async calls) 					| wip 		 	|

## API

MPI program are supported by default but you need to link you program using ```libreplication.so``` after installing **EntangledMPI**. 

Addition to this two malloc wrappers are avalaible which should be used instead of ```malloc``` and to copy addresses from one pointer to another.

+ ```void rep_malloc(void **, size_t)``` used to allocate memory from heap.
+ ```void rep_free(void **)``` used to free memory.

### Example

```c
int *ptr, *dup_ptr;
rep_malloc(&ptr, sizeof(int));

*ptr = 10;

rep_free(&ptr);
ptr = NULL;
```

## Building EntangledMPI

### Requirement
1. Autoconf >= 2.52
2. Automake >= 1.6.0
3. Libtool >= 2.4.2
4. MPI Compiler (mpicc) with ULFM support

```bash
./autogen.sh
mkdir build && cd build
../configure --disable-stack-protector [CC=mpicc] [--prefix=<directory>]
# CC=mpicc is optional but might be required in some situations
make [-j N]
# use an integer value of N for parallel builds
make install
```



## Examples and Tutorials

This framework is compatible with all MPI programs you only need to replace all your ```malloc``` calls to ```rep_malloc``` and ```free``` to ```rep_free``` and link your program with this library.

You can also view the [test](test/) folder for some example MPI programs.

**IMPORTANT NOTE:**

This framework will only run if the program is compiled dynamically i.e. using "-dynamic" flag, with stack protection disabled i.e. using "-fno-stack-protector" flag and disabled [ASLR](https://en.wikipedia.org/wiki/Address_space_layout_randomization) (Address space layout randomization) using "sudo sysctl kernel.randomize_va_space=0" to disable ASLR only for your session.

[Stackoverflow help](https://askubuntu.com/questions/318315/how-can-i-temporarily-disable-aslr-address-space-layout-randomization)