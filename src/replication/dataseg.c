#include "dataseg.h"

/* Extracting memory location of useful data segment part.
*  use "nm <EXECUTABLE_FILE>' command to get all symbols in the object file.
*
* What does "useful" mean?
*   Data segment (initialised and uninitialised) also contains "Global Symbol Table",
*   if this table data is migrated and overwritten "Procedure Links" will be corrupted
*   in the replica process.
*
* Example of segments (extracted using nm command)
* Address 			Symbol Type		Symbol Name				Data Segment
* 0000000000601338 	d 				_GLOBAL_OFFSET_TABLE_	initialized data section
* 0000000000601380 	D 				__data_start			initialized data section
* 0000000000601408 	D 				_edata					initialized data section
* 0000000000601408 	B 				__bss_start				uninitialized data section
* 00000000006014d0 	B 				_end					uninitialized data section
*
* Address will change at every compilation.
*/

extern __data_start;		// Initialized Data Segment 	: Start
extern _edata;				// Initialized Data Segment		: End

extern __bss_start;			// Uninitialized Data Segment	: Start
extern _end;				// Uninitialized Data Segment	: End

extern Node node;

int transfer_data_seg(MPI_Comm job_comm) {
	log_i("Data Segment Init.");

	debug_log_i("Node Address: %p | __data_start: %p | _edata: %p | __bss_start: %p | _end: %p", &node, &__data_start, &_edata, &__bss_start, &_end);
	
	PMPI_Barrier(job_comm);
	transfer_init_data_seg(job_comm);
	transfer_uninit_data_seg(job_comm);

	log_i("Data Seg Transfer Ended.");
}

int transfer_init_data_seg(MPI_Comm job_comm) {

	debug_log_i("Initialised Data Segment Init");

	int countBytes = ((char *)&_edata - (char *)&__data_start);
	// TODO: Check if countBytes + 1 is valid.
	int success;

	success = PMPI_Bcast(&__data_start, countBytes, MPI_BYTE, 0, job_comm);

	int ranktt;
	PMPI_Comm_rank(job_comm, &ranktt);
	debug_log_i("Sending init data seg: Size: %d | Start Address: %p | Job_comm Rank: %d", countBytes, &__data_start, ranktt);

	/*if(rank == 0) {
		success = MPI_Send(&__data_start, countBytes, MPI_BYTE, 1, 1, comm);
	}
	else {
		success = MPI_Recv(&__data_start, countBytes, MPI_BYTE, 0, 1, comm, MPI_STATUS_IGNORE);
	}

	#ifdef DEBUG
	printf("Rank: %d | Init Seg Bytes: %d\n", rank, countBytes);
	#endif*/

	return success;
}

int transfer_uninit_data_seg(MPI_Comm job_comm) {

	debug_log_i("UnInitialised Data Segment Init");

	int countBytes = ((char *)&_end - (char *)&__bss_start);
	int success;
	
	success = PMPI_Bcast(&__bss_start, countBytes, MPI_BYTE, 0, job_comm);

	int ranktt;
	PMPI_Comm_rank(job_comm, &ranktt);

	debug_log_i("Sending uninit data seg: Size: %d | Start Address: %p | Job_comm Rank: %d", countBytes, &__bss_start, ranktt);


	/*#ifdef DEBUG
	printf("Rank: %d | UnInit Seg Bytes: %d\n", rank, countBytes);
	#endif

	if(rank == 0) {
		success = MPI_Send(&__bss_start, countBytes, MPI_BYTE, 1, 2, comm);
	}
	else {
		success = MPI_Recv(&__bss_start, countBytes, MPI_BYTE, 0, 2, comm, MPI_STATUS_IGNORE);
	}*/

	// Uninit Start: 0xa0b230 | End: 0xa51a60
	// Uninit Start: 0xa0b230 | End: 0xa51a60

	return success;
}
