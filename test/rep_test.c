#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include "src/replication/rep.h"

#include <pthread.h>

int initSeg = 80;

extern Node node;

void f4(int *a) {

	sleep(10);
	MPI_Send(a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

	sleep(10);
	MPI_Send(a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

	sleep(10);
	MPI_Send(a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

	sleep(10);
	MPI_Send(a, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
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
	int rank, size = 9, len, oo = 9, sendRecvData;
	char procName[100];
	int *a, *b;
	char *c;

	MPI_Init(&argc, &argv);

	MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

	pid_t pid;

	pid = getpid();

	printf("Process ID: %d\n", pid);

	/*if(node.rank == 1)
		kill(pid, SIGBUS);*/

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	printf("[USER CODE] Rank Address: %p\n", &rank);

	/*if(rank == 1)
		readProcMapFile();*/

	if(rank == 0) {
		rep_malloc(&a, sizeof(int));
		*a = 43;
		oo = 96;
		initSeg = 1945;
		sendRecvData = 2019;
		printf("[User Code] Container Address: %p | Rank: %d\n", &a, rank);
		f1(&sendRecvData);
		
	}
	else {
		rep_malloc(&b, sizeof(int));
		*b = 34;
		oo = 77;
		initSeg = 200000;
		printf("[User Code] Container Address: %p | Rank: %d\n", &a, rank);

		sleep(10);
		MPI_Recv(&sendRecvData, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		printf("[User Code] BEFORE 1 | Rank: %d\n", rank);

		sleep(10);
		MPI_Recv(&sendRecvData, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		printf("[User Code] BEFORE 2 | Rank: %d\n", rank);

		sleep(10);
		MPI_Recv(&sendRecvData, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		printf("[User Code] BEFORE 3 | Rank: %d\n", rank);

		sleep(10);
		MPI_Recv(&sendRecvData, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		printf("[User Code] BEFORE 4 | Rank: %d\n", rank);
	}

	printf("[User Code] Rank: %d\n", rank);

	
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if(rank == 0)
		printf("[Heap Seg] Rank: %d | [Users program] Value: %d | address: %p\n", rank, *a, a);

	if(rank == 1)
		printf("[Heap Seg] Rank: %d | [Users program] Value: %d | address: %p\n", rank, *b, b);

	printf("[Data Seg] Rank: %d | [Users program] Value: %d | address: %p\n", rank, initSeg, &initSeg);
	printf("[Stack Seg] Rank: %d | [Users program] Value: %d | address: %p\n", rank, oo, &oo);
	printf("[Send recv Data] Rank: %d | [Users program] Value: %d\n", rank, sendRecvData);
	
	if(rank == 0) {
		if(*a == 43)
			printf("[Heap Seg] Rank: %d | SUCCESS\n", rank);
		else
			printf("[Heap Seg] Rank: %d | FAIL\n", rank);

		if(initSeg == 1945) 
			printf("[Data Seg] Rank: %d | SUCCESS\n", rank);
		else
			printf("[Data Seg] Rank: %d | FAIL\n", rank);
		
		if(oo == 96)
			printf("[Stack Seg] Rank: %d | SUCCESS\n", rank);
		else
			printf("[Stack Seg] Rank: %d | FAIL\n", rank);
	}
	else if(rank == 1) {
		if(*b == 34)
			printf("[Heap Seg] Rank: %d | SUCCESS\n", rank);
		else
			printf("[Heap Seg] Rank: %d | FAIL\n", rank);

		if(initSeg == 200000) 
			printf("[Data Seg] Rank: %d | SUCCESS\n", rank);
		else
			printf("[Data Seg] Rank: %d | FAIL\n", rank);
		
		if(oo == 77)
			printf("[Stack Seg] Rank: %d | SUCCESS\n", rank);
		else
			printf("[Stack Seg] Rank: %d | FAIL\n", rank);

		if(sendRecvData == 2019)
			printf("[Send recv Data] Rank: %d | SUCCESS\n", rank);
		else
			printf("[Send recv Data] Rank: %d | FAIL\n", rank);
	}

	MPI_Finalize();
}
