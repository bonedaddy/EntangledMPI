#include <mpi.h>
#include <stdio.h>

#include "src/shared.h"
#include "src/replication/rep.h"
#include "src/misc/file.h"
#include "src/mpi/comm.h"
#include "src/checkpoint/full_context.h"

#define REP_THREAD_SLEEP_TIME 3

jmp_buf context;
address stackHigherAddress, stackLowerAddress;
enum MapStatus map_status;

/* pthread mutex */
pthread_mutex_t global_mutex;
pthread_mutex_t rep_time_mutex;

Job *job_list;
Node node;

char *map_file = "./replication.map";
char *ckpt_file = "./ckpt/rank-%d.ckpt";

// Restore from checkpoint files: YES | Do not restore: NO
enum CkptBackup ckpt_backup;

// this comm contains sender and receiver nodes during replication.
MPI_Comm job_comm;

extern Malloc_list *head;

void *rep_thread_init(void *_stackHigherAddress) {
	 	// Higher end address (stack grows higher to lower address)
	stackHigherAddress = (address *)_stackHigherAddress;
	
	time_t last_update;
	int rep_flag;
	
	// First run does not create replicas, only correct ranks are initialised.
	set_last_update(map_file, &last_update);
	
	//printf("Stack Start Address: %p | Value: %p\n", &stackHigherAddress, stackHigherAddress);

	while(1) {

		// Start checking for any map file updates.
		if(is_file_modified(map_file, &last_update, &ckpt_backup)) {
			
			debug_log_i("Inside Modified file");

			parse_map_file(map_file, &job_list, &node, &ckpt_backup);
			
			if( create_migration_comm(&job_comm, &rep_flag, &ckpt_backup) ) {
				
				pthread_mutex_lock(&rep_time_mutex);
				
				map_status = MAP_UPDATED;
				debug_log_i("Modified Signal ON");
				
				pthread_mutex_lock(&global_mutex);
				log_i("Main thread blocked");

				if(ckpt_backup == BACKUP_YES) {
					// Checkpoint Backup
					log_i("Checkpoint restore started...");

					// ckpt restore code
					init_ckpt_restore(ckpt_file);

					ckpt_backup = BACKUP_NO;	// Very imp, do not miss this.
				}
				else {
				
					// Checkpoint creation
					if(node.node_checkpoint_master == YES)
						init_ckpt(ckpt_file);

					// Replica creation
					if(rep_flag)
						init_rep(job_comm);
				}

				map_status = MAP_NOT_UPDATED;

				pthread_mutex_unlock(&global_mutex);
				pthread_mutex_unlock(&rep_time_mutex);
			}
			
		}
		sleep(REP_THREAD_SLEEP_TIME);
		debug_log_i("File Check Sleep");
	}
}

int MPI_Init(int *argc, char ***argv) {
	int ckpt_bit;
	PMPI_Init(argc, argv);

	if(argv == NULL) {
		debug_log_e("You must pass &argv to MPI_Init()");
		exit(0);
	}

	address stackStart;

	// Getting RBP only works if optimisation level is zero (O0).
	// O1 removes RBP's use.
	/*Init_Rep(stackStart);	// a macro to fetch RBP of main function.
	if(stackStart == 0) {
		// OS kept values of program arguments this this address which is below main function frame.
		// Writable to the user and same for each program. No harm to overwrite this data as it is same for
		// all the nodes. [Same arguments passed]
		stackStart = **argv;
	}*/

	stackStart = *argv;

	debug_log_i("Address Stack new: %p", stackStart);
	// Lock global mutex. This mutex will always be locked when user program is executing.
	pthread_mutex_init(&global_mutex, NULL);
	pthread_mutex_init(&rep_time_mutex, NULL);
	
	pthread_mutex_lock(&global_mutex);

	// job_list and node declared in shared.h
	init_node(map_file, &job_list, &node);

	ckpt_bit = does_ckpt_file_exists(ckpt_file);

	if(ckpt_bit) {
		ckpt_backup = BACKUP_YES;
	}

	pthread_t tid;
	pthread_create(&tid, NULL, rep_thread_init, stackStart);

	if(ckpt_bit) {
		while(is_file_update_set() != 1);
	}

	PMPI_Barrier(MPI_COMM_WORLD);
}

int MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
	debug_log_i("In MPI_Send()");
	is_file_update_set();
	debug_log_i("In MPI_Send() after is_file_update_set");
	// Not fault tolerant
	if(node.node_checkpoint_master == YES) {
		for(int i=0; i<job_list[dest].worker_count; i++) {
			//printf("[Rank: %d] Job List: %d\n", node.rank, (job_list[dest].rank_list)[i]);
			debug_log_i("SEND: Data: %d", *((int *)buf));
			PMPI_Send(buf, count, datatype, (job_list[dest].rank_list)[i], tag, comm);
		}
	}
}

int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status) {
	debug_log_i("In MPI_Recv()");
	is_file_update_set();

	PMPI_Recv(buf, count, datatype, (job_list[source].rank_list)[0], tag, comm, status);
	debug_log_i("RECV: Data: %d", *((int *)buf));
}

int MPI_Comm_rank(MPI_Comm comm, int *rank) {
	*rank = node.job_id;
}

int MPI_Comm_size(MPI_Comm comm, int *size) {
	*size = node.jobs_count;
}
