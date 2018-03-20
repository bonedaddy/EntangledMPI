#include <stdio.h>
#include "src/replication/rep.h"

#define DATA_SIZE 4

extern Node node;

int main(int argc, char** argv){
	MPI_Init(&argc, &argv);

	int rank, size, *arr_send, *arr_recv;

	MPI_Comm comm = MPI_COMM_WORLD;

	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &rank);

	if(node.rank == 1)
		exit(EXIT_FAILURE);

	if(rank == 0) {
		rep_malloc(&arr_send, sizeof(int) * size * DATA_SIZE);

		for(int i=0; i<size * DATA_SIZE; i++) {
			arr_send[i] = i;
		}
	}

	rep_malloc(&arr_recv, sizeof(int) * DATA_SIZE);

	MPI_Scatter(arr_send, DATA_SIZE, MPI_INT, arr_recv, DATA_SIZE, MPI_INT, 0, comm);

	int success = 1;
	for(int i=0; i<DATA_SIZE; i++) {
		if(arr_recv[i] != rank * DATA_SIZE + i)
			success = 0;
	}

	if(success) {
		printf("Rank: %d SUCCESS\n", rank);
	}
	else {
		printf("Rank: %d FAIL\n", rank);
	}


	MPI_Finalize();
	return 1;
}
