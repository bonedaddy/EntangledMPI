#include <stdio.h>
#include "src/replication/rep.h"

#include <pthread.h>

int a = 80;
int b;



void f4(int *a) {

	*a = 96;

	sleep(10);
	MPI_Send(&a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

	sleep(10);
	MPI_Send(&a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

	sleep(10);
	MPI_Send(&a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

	sleep(10);
	MPI_Send(&a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
}

void f3(int *a) {
	f4(a);
}

void f2(int *a) {
	f3(a);
} 

void f1(int *a) {
	f2(a);
}

int main(int argc, char** argv){
	int rank, size, len, oo = 9;
	char procName[100];
	int *a, *b;
	char *c;

	//PMPI_Init(&argc, &argv);

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	//stackMig(2);
	if(rank == 0) {
		rep_malloc(&a, sizeof(int));
		*a = 43;
		f1(&oo);
	}
	else {
		oo = 77;

		sleep(10);
		MPI_Send(&rank, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

		sleep(10);
		MPI_Send(&rank, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

		sleep(10);
		MPI_Send(&rank, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

		sleep(10);
		MPI_Send(&rank, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
	}

	printf("[User Code] Rank: %d\n", rank);

	

	
	//readProcMapFile();
	//printf("In Main | main thread\n");

	MPI_Barrier(MPI_COMM_WORLD);

	


	printf("Rank: %d | [Users program] Value: %d | address: %p\n", rank, oo, &oo);
	if(rank == 0)
		printf("Rank: %d | [Users program] Heap Val: %d\n", rank, *a);
	//printf("Init: %p | Uninit: %p\n", &a, &b);


	MPI_Finalize();

	/*
	MPI_Comm comm = MPI_COMM_WORLD;

	MPI_Init(&argc, &argv);

	MPI_Replicate();

	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &rank);

	printf("Hello after size and rank functions\n");

	MPI_Barrier(comm);

	//int *temp = (int *) malloc(sizeof(int)*20);

	MPI_Get_processor_name(procName,&len);

	printf("Hello from Process %d of %d on %s\n", rank, size, procName);


	MPI_Finalize();*/
}
