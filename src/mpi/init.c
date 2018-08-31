#include <mpi.h>

#ifdef OPEN_MPI
#include <mpi-ext.h>
#endif

#include <stdio.h>
#include <signal.h>
#include <stdatomic.h>

#include "src/shared.h"
#include "src/replication/rep.h"
#include "src/misc/file.h"
#include "src/misc/network.h"
#include "src/mpi/comm.h"
#include "src/checkpoint/full_context.h"
#include "src/mpi/ulfm.h"
#include "src/mpi/async.h"

#define REP_THREAD_SLEEP_TIME 3
#define NO_TRIALS 10
#define DEFINE_BUFFER(_buf, buf) void **_buf = buf;
#define SET_RIGHT_S_BUFFER(_buffer) __pass_sender_cont_add ? *_buffer : _buffer
#define SET_RIGHT_R_BUFFER(_buffer) __pass_receiver_cont_add ? *_buffer : _buffer

jmp_buf context;
address stackHigherAddress, stackLowerAddress;
enum MapStatus map_status;

/* pthread mutex */
pthread_mutex_t global_mutex;
pthread_mutex_t rep_time_mutex;
pthread_mutex_t comm_use_mutex;

pthread_mutexattr_t attr_comm_to_use;

Job *job_list;
Node node;
int *rank_2_job = NULL;
// should be atomic
atomic_int __request_pending;

char *map_file = "./replication.map";
char *network_stat_file = "./network.stat";

// Restore from checkpoint files: YES | Do not restore: NO
enum CkptRestore ckpt_restore;

// this comm contains sender and receiver nodes during replication.
MPI_Comm job_comm;

MPI_Errhandler ulfm_err_handler;

// Non zero when container address is passed to MPI_* functions.
// When pointers are passed to functions by value (addresses returned by malloc). 
// These pointer values are pushed to the stack during function execution.
// During stack replication or checkpointing, these address are send
// to the corresponding node where these addresses are invalid.
int __pass_sender_cont_add;
int __pass_receiver_cont_add;

// This variable is used in non-collective calls to ignore process failure errors.
// Error handler uses collective calls and not call processes/ranks will be calling 
// *send and *recv. Hance it could result in a deadlock, so better ignore and
// correct comm in a collective call (happening sometime after this call)
int __ignore_process_failure;

int mutex_status = 1;
int global_mutex_status;

int *rank_ignore_list;

int __process_shrinking_pending;

extern Malloc_list *head;

void *___temp_add;

void *__blackhole_address;

time_t last_file_update;

int replication_idx;

extern int gdb_val;

double ___rep_time[MAX_REP];
int ___rep_counter = -1;

double ___ckpt_time[MAX_CKPT];
int ___ckpt_counter = -1;

// Common function logic for collective MPI_* functions

#define DECLARE_VARS()															\
	int mpi_status = MPI_SUCCESS;												\
	int flag;																	\
	int total_trails = 0;														\
	MPI_Comm *comm_to_use;														\
	enum NodeCheckpointMaster ckpt_master_backup;		

#define ACTIVATE_COMM_AND_BKUP_MASTER() 										\
	if(comm == MPI_COMM_WORLD) {												\
		comm_to_use = &(node.rep_mpi_comm_world);								\
	}																			\
	ckpt_master_backup = node.node_checkpoint_master;

#define TRIAL_INC_AND_CHECK()													\
	total_trails++;																\
	if(total_trails >= NO_TRIALS) {												\
		log_e("Total Trails exceeds. Aborting.");								\
		PMPI_Abort(node.rep_mpi_comm_world, 10);								\
	}

#define SET_FLAG_ON_SUCCESS()													\
	if(MPI_SUCCESS == mpi_status) {												\
		flag = -1;																\
	}

// What if all the compute nodes died.
// value of flag = MPI_SUCCESS and comm_agree
// will return same values
// change in node_checkpoint_master means that
// this node is made master recently.
// Hence PMPI_Reduce did not succeed.
#define DO_COMM_AGREE()															\
	while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag)) {					\
		debug_log_i("Comm Agree Failed");										\
		if(ckpt_master_backup != node.node_checkpoint_master) {					\
			flag = node.static_rank;											\
			ckpt_master_backup = node.node_checkpoint_master;					\
		}																		\
	}

#define IF_OPERATION_FAILED_ELSE(msg, el) 										\
	if(flag != -1) {															\
		debug_log_i(msg);														\
		continue;																\
	}																			\
	else {																		\
		el 																		\
	}

#define CONTINUE_TILL_SUCCESS() 												\
	while(flag != -1)

// If failed node is root node in node.world_job_comm, data is lost 
// and main operation needs to be done again before doing bcast.
#define HANDLE_FAILURE_ON_JOB_COMM(msg)											\
	debug_log_i("[Value flag]: %d", flag);										\
	if(flag != -1) {															\
		debug_log_i(msg);										\
		if(is_failed_node_world_job_comm_root()) {								\
			debug_log_i("root node died. Doing main communication again");		\
			break;																\
		}																		\
	}

// END of common logic for collective calls

void __attribute__((constructor)) calledFirst(void)
{	
	int a;
    
    stackHigherAddress = &a;
    /*int hang = 1;
    debug_log_i("Program Hanged");
	while(hang) {
		sleep(2);
	}*/
}

/*void *rep_thread_init(void *_stackHigherAddress) {
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
			
			log_i("Inside Modified file");

			mutex_status = 1;

			// while(1) {
			// 	mutex_status = pthread_mutex_trylock(&comm_use_mutex);
			// 	mutex_status += __request_pending;

			// 	// while to handle all process failure for MPI_Allgather
			// 	debug_log_i("All Reduce doing: %d", mutex_status);
			// 	// TODO [IMPORTANT]: Will not work in case of process failure.
			// 	PMPI_Allreduce(&mutex_status, &global_mutex_status, 1, MPI_INT, MPI_SUM, node.rep_mpi_comm_world);
			// 	log_i("All Reduce hogaya: %d", global_mutex_status);
			// 	if(global_mutex_status == 0) {
			// 		// Everyone acquired lock.
			// 		break;
			// 	} else {
			// 		debug_log_i("No Success: %d", mutex_status);
			// 		if(mutex_status == 0) {
			// 			pthread_mutex_unlock(&comm_use_mutex);
			// 			//mutex_status = 1;
			// 		}
					
			// 	}
			// 	sleep(2);
			// }
			log_i("Roll Call: Rank: %d", node.rank);

			parse_map_file(map_file, &job_list, &node, &ckpt_backup);

			log_i("Before update %d", global_mutex_status);
			update_comms(); 	// Maybe remove comm_use_mutex locking inside this function.

			log_i("after update comm");
			
			if(create_migration_comm(&job_comm, &rep_flag, &ckpt_backup) ) {

				if (!rep_flag && ckpt_backup == BACKUP_NO) {
					// Do not block the thread unnecessarily.
					//pthread_mutex_unlock(&comm_use_mutex);
					continue;
				}

				log_i("Just inside cr mig");
				
				pthread_mutex_lock(&rep_time_mutex);
				debug_log_i("rep_time_mutex locked");
				
				map_status = MAP_UPDATED;
				//pthread_mutex_unlock(&comm_use_mutex);
				log_i("Modified Signal ON");
				
				pthread_mutex_lock(&global_mutex);
				log_i("Main thread blocked: rep: %d", rep_flag);

				if(ckpt_backup == BACKUP_YES) {
					// Checkpoint restore
					log_i("Checkpoint restore started...");

					// ckpt restore code
					init_ckpt_restore(ckpt_file);

					ckpt_backup = BACKUP_NO;	// Very imp, do not miss this.
				}
				else {
				
					// Checkpoint creation
					// if(node.node_checkpoint_master == YES)
					// 	init_ckpt(ckpt_file);

					// Replica creation
					if(rep_flag)
						init_rep(job_comm);
				}

				map_status = MAP_NOT_UPDATED;

				//pthread_mutex_unlock(&global_mutex);
				//pthread_mutex_unlock(&rep_time_mutex);

				log_i("Unlocked everything");
			}
			else {
				//pthread_mutex_unlock(&comm_use_mutex);
			}
			
		}
		sleep(REP_THREAD_SLEEP_TIME);
		//debug_log_i("File Check Sleep");
	}
}*/

int MPI_Init(int *argc, char ***argv) {

	int ckpt_restore_bit;
	//int thread_level_provided;

	/*if(mcheck(dyn_mem_err_hook) != 0) {
		log_e("mcheck failed.");
	}
	else {
		log_i("############# MCHECK USED.");
	}*/

	PMPI_Init(argc, argv);

	PMPI_Comm_create_errhandler(rep_errhandler, &ulfm_err_handler);
	//PMPI_Comm_set_errhandler(MPI_COMM_WORLD, ulfm_err_handler);

	PMPI_Comm_dup(MPI_COMM_WORLD, &(node.rep_mpi_comm_world));
	PMPI_Comm_set_errhandler(node.rep_mpi_comm_world, ulfm_err_handler);
	//PMPI_Comm_set_errhandler(MPI_COMM_WORLD, ulfm_err_handler);

	// job_list and node declared in shared.h
	init_node(map_file, &job_list, &node);

	log_i("Initialising MPI.");
	
	/*if(argv == NULL) {
		debug_log_e("You must pass &argv to MPI_Init()");
		exit(0);
	}*/

	address temp_stackHigherAddress;

	// Getting RBP only works if optimisation level is zero (O0).
	// O1 removes RBP's use.
	/*Init_Rep(stackStart);	// a macro to fetch RBP of main function.
	if(stackStart == 0) {
		// OS kept values of program arguments this this address which is below main function frame.
		// Writable to the user and same for each program. No harm to overwrite this data as it is same for
		// all the nodes. [Same arguments passed]
		stackStart = **argv;
	}*/

	//stackStart = *argv;

	/*PMPI_Allreduce(&stackHigherAddress, &temp_stackHigherAddress, sizeof(address), MPI_BYTE, MPI_BOR, node.rep_mpi_comm_world);

	if(stackHigherAddress != temp_stackHigherAddress) {
		log_e("Stack shift detected. Stack starts from different addresses in some nodes: %p", stackHigherAddress);
		PMPI_Abort(node.rep_mpi_comm_world, 100);
		exit(2);
	}

	log_i("Address Stack new: %p | argv add: %p", stackHigherAddress, argv);*/

	set_last_file_update(map_file, &last_file_update);
	ckpt_restore_bit = does_ckpt_file_exists(CKPT_FILE_NAME);

	if(ckpt_restore_bit) {
		ckpt_restore = RESTORE_YES;
	}

	// Save hostname of this host for process manager and fault injector.
	network_stat_init(network_stat_file);

	replication_idx = 0;
	save_rep_and_stack_info(replication_idx);

	// Init blackhole
	__blackhole_address = malloc(sizeof(char) * 10000000);

	if(ckpt_restore_bit) {
		is_file_update_set();
	}

	debug_log_i("Before Barrier");
	PMPI_Barrier(node.rep_mpi_comm_world);
}

int MPI_Finalize(void) {
	debug_log_i("MPI_Finalize() call");
	free(rank_ignore_list);
	free(__blackhole_address);

	// No fault tolerance here.
	int master_of_master = job_list[0].rank_list[0];
	int global_size;

	PMPI_Comm_size(node.rep_mpi_comm_world, &global_size);

	double *time = malloc(sizeof(double) * global_size);
	
	int n_rep;
	for(n_rep = 0; n_rep<___rep_counter + 1; n_rep++) {
		int valid_times = 0;
		double sum_time = 0;
		PMPI_Gather(___ckpt_time + n_rep, 1, MPI_DOUBLE, time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
		for(int i = 0; i<global_size; i++) {
			if(time[i] != 0) {
				valid_times++;
				sum_time += time[i];
			}
		}
		if(valid_times == 0) {
			___ckpt_time[n_rep] = 0;
		} else {
			___ckpt_time[n_rep] = sum_time / valid_times;
		}

		valid_times = 0;
		sum_time = 0;

		PMPI_Gather(___rep_time + n_rep, 1, MPI_DOUBLE, time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
		for(int i = 0; i<global_size; i++) {
			if(time[i] != 0) {
				valid_times++;
				sum_time += time[i];
			}
		}
		if(valid_times == 0) {
			___rep_time[n_rep] = 0;
		} else {
			___rep_time[n_rep] = sum_time / valid_times;
		}

		if(node.static_rank == 0) {
			log_i("[%d]: Rep Time: %fs | Ckpt Time: %fs", n_rep, ___rep_time[n_rep], ___ckpt_time[n_rep]);
		}
	}

	free(time);

	return MPI_SUCCESS;//PMPI_Finalize();
}

int MPI_Barrier(MPI_Comm comm) {
	/*if(comm == MPI_COMM_WORLD) {
		return PMPI_Barrier(node.rep_mpi_comm_world);
	}*/
	return PMPI_Barrier(node.rep_mpi_comm_world);
}

int MPI_Comm_rank(MPI_Comm comm, int *rank) {
	*rank = node.job_id;
}

int MPI_Comm_size(MPI_Comm comm, int *size) {
	*size = node.jobs_count;
}

int MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
	debug_log_i("In MPI_Send()");
	//is_file_update_set();
	acquire_comm_lock();
	debug_log_i("In MPI_Send() after is_file_update_set");

	__ignore_process_failure = 1;

	MPI_Comm *comm_to_use;

	if(comm == MPI_COMM_WORLD) {
		// So that comm err handler do not identify it as rep_mpi_comm_world
		// It will ignore it and will not enter the if statement.
		// Note: If it enters, process will hang as functions inside are
		// collective.
		//PMPI_Comm_dup(node.rep_mpi_comm_world, &comm_to_use);

		comm_to_use = &(node.rep_mpi_comm_world);
	}

	DEFINE_BUFFER(buffer, buf);

	int mpi_status;
	int type_size;

	PMPI_Type_size(datatype, &type_size);
	void *temp_mem = malloc(type_size * count * job_list[dest].worker_count);


	for(int i=0; i<job_list[dest].worker_count; i++) {
		if(rank_ignore_list[ (job_list[dest].rank_list)[i] ] == 1) {
			continue;
		}
		void *this_mem = ((char *)temp_mem) + i * (type_size * count);
		memcpy( this_mem, SET_RIGHT_S_BUFFER(buffer), type_size * count );
		//printf("[Rank: %d] Job List: %d\n", node.rank, (job_list[dest].rank_list)[i]);
		debug_log_i("SEND: Data: %d to %d | ori dest: %d", *((int *)buf), (job_list[dest].rank_list)[i], dest);
		mpi_status = PMPI_Send(this_mem, count, datatype, (job_list[dest].rank_list)[i], SET_TAG(node.rank, tag), *comm_to_use);

		if(mpi_status != MPI_SUCCESS) {
			debug_log_i("MPI_Send Failed [Dest: %d]", (job_list[dest].rank_list)[i]);
		}
		else {
			debug_log_i("MPI_Send Success [Dest: %d]", (job_list[dest].rank_list)[i]);
		}
	}

	free(temp_mem);

	__ignore_process_failure = 0;

	release_comm_lock();

	debug_log_i("Still Alive");

	return mpi_status;
}

int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status) {
	debug_log_i("In MPI_Recv()");
	//is_file_update_set();
	acquire_comm_lock();

	__ignore_process_failure = 1;

	//int sender = 0;
	int mpi_status;
	MPI_Comm *comm_to_use;

	if(comm == MPI_COMM_WORLD) {
		//PMPI_Comm_dup(node.rep_mpi_comm_world, &comm_to_use);

		comm_to_use = &(node.rep_mpi_comm_world);
	}

	// DEBUG
	/*int rank;
	PMPI_Comm_rank(*comm_to_use, &rank);
	debug_log_i("This rank: %d", rank);
	
	DEFINE_BUFFER(buffer, buf);
	mpi_status = PMPI_Recv(SET_RIGHT_R_BUFFER(buffer), count, datatype, (job_list[source].rank_list)[0], tag, *comm_to_use, status);
	debug_log_i("RECV: Data: %d", *((int *)buf));

	if(mpi_status != MPI_SUCCESS) {
		debug_log_i("MPI_Recv Failed [Dest: %d]", (job_list[source].rank_list)[0]);
		//sender++;
	}
	else {
		debug_log_i("MPI_Recv Success [Dest: %d]", (job_list[source].rank_list)[0]);
	}*/

	DEFINE_BUFFER(buffer, buf);
	int recv_source;
	for(int i=0; i<job_list[source].worker_count; i++) {
		if(rank_ignore_list[ (job_list[source].rank_list)[i] ] == 1) {
			continue;
		}

		// TODO: IMPORTANT: CREATE SEPERATE BUFFER FOR EACH WORKER.
		recv_source = (job_list[source].rank_list)[i];
		mpi_status = PMPI_Recv(SET_RIGHT_R_BUFFER(buffer), count, datatype, recv_source, SET_TAG(recv_source, tag), *comm_to_use, status);
		
		if(mpi_status != MPI_SUCCESS) {
			debug_log_i("MPI_Recv Failed [Dest: %d]", recv_source);
			//sender++;
		}
		else {
			debug_log_i("MPI_Recv Success [Dest: %d]", recv_source);
		}
	}

	__ignore_process_failure = 0;

	release_comm_lock();

	return mpi_status;
}

int MPI_Scatter(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
	debug_log_i("MPI_Scatter Call");
	int mpi_status = MPI_SUCCESS;
	int flag;
	int total_trails = 0;
	MPI_Comm *comm_to_use;

	is_file_update_set();
	acquire_comm_lock();

	DEFINE_BUFFER(sbuffer, sendbuf);
	DEFINE_BUFFER(rbuffer, recvbuf);

	if(comm == MPI_COMM_WORLD) {
		comm_to_use = &(node.rep_mpi_comm_world);
	}

	do {

		mpi_status = MPI_SUCCESS;

		total_trails++;

		if(total_trails >= NO_TRIALS) {
			log_e("Total Trails exceeds. Aborting.");
			PMPI_Abort(node.rep_mpi_comm_world, 10);
		}

		int pp;
		PMPI_Comm_rank(*comm_to_use, &pp);
		debug_log_i("Comm_to_use rank: %d", pp);

		if(node.active_comm != MPI_COMM_NULL) {
			int ll;
			PMPI_Comm_size(node.active_comm, &ll);
			debug_log_i("node.active_comm Size: %d", ll);

			debug_log_i("RIGHT BUFFER: %d", SET_RIGHT_S_BUFFER(sbuffer), SET_RIGHT_R_BUFFER(rbuffer));
			mpi_status = PMPI_Scatter(SET_RIGHT_S_BUFFER(sbuffer), sendcount, sendtype, SET_RIGHT_R_BUFFER(rbuffer), recvcount, recvtype, root, node.active_comm);	
		}

		flag = (MPI_SUCCESS == mpi_status);

		// To correct the comms
		// why "while"? check MPI_Bcast.
		while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag)) {
			debug_log_i("First Comm agree");
			//flag = 0;
			//continue;
		}
		// To perform agree on flag
		//PMPIX_Comm_agree(comm_to_use, &flag);

		if(!flag) {
			debug_log_i("MPI_Scatter Failed");
			flag = 0;
			continue;
		}
		else {

			/*if(node.rank == 1)
				raise(SIGKILL);*/
			
			do {

				debug_log_i("Doing Bcast");

				
				mpi_status = PMPI_Bcast(SET_RIGHT_R_BUFFER(rbuffer), recvcount, recvtype, 0, node.world_job_comm);
				debug_log_i("Aftter bcast: MPI_Status: %d", mpi_status == MPI_SUCCESS);
				flag = (MPI_SUCCESS == mpi_status);
				
				// To correct the comms
				while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag));
				// To perform agree on flag
				//PMPIX_Comm_agree(comm_to_use, &flag);

				debug_log_i("[Value flag]: %d", flag);
				if(!flag) {
					debug_log_i("MPI_Bcast Failed");

					// If failed node is root node in node.world_job_comm, data is lost 
					// and main operation needs to be done again before doing bcast.
					if(is_failed_node_world_job_comm_root()) {
						debug_log_i("node.world_job_comm root node died. Doing main communication again");
						flag = 0;
						break;
					}
				}

			} while(!flag);
			
		}

		

	} while(!flag);

	release_comm_lock();

	return MPI_SUCCESS;
}

int MPI_Bcast(void *buf, int count, MPI_Datatype datatype, int root, MPI_Comm comm) {
	debug_log_i("In MPI_Bcast()");
	int mpi_status = MPI_SUCCESS;
	int flag;
	int total_trails = 0;
	MPI_Comm *comm_to_use;

	is_file_update_set();
	acquire_comm_lock();

	// Hack to pass pointers
	DEFINE_BUFFER(buffer, buf);

	if(comm == MPI_COMM_WORLD) {
		comm_to_use = &(node.rep_mpi_comm_world);
	}

	do {
		total_trails++;

		if(total_trails >= NO_TRIALS) {
			log_e("Total Trails exceeds. Aborting.");
			PMPI_Abort(node.rep_mpi_comm_world, 10);
		}

		debug_log_i("Starting bcast: Comm: %p | node.rep_comm: %p | Comm to use: %p", comm, node.rep_mpi_comm_world, *comm_to_use);

		int root_rank = job_list[root].rank_list[0];


		mpi_status = PMPI_Bcast(SET_RIGHT_S_BUFFER(buffer), count, datatype, root_rank, *comm_to_use);


		flag = (MPI_SUCCESS == mpi_status);

		debug_log_i("bcast done Success: %d", flag);

		// To correct the comms [Refer to Issue #29 on Github]
		// while loop should only run twice:
		//  1. To correct (shrink) the comm incase of failure
		//  2. To agree on flag value
		while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag)) {
			debug_log_i("First Comm agree");
			//flag = 0;
			//continue; 	// This was a bad idea
		}
		// To perform agree on flag
		//PMPIX_Comm_agree(comm_to_use, &flag);  	// Initial thinking was correct

		if(!flag) {
			debug_log_i("MPI_Bcast Failed");
			flag = 0;
			continue;
		}

	} while(!flag);

	release_comm_lock();

	return MPI_SUCCESS;


	/*int color = 1;
	int rank_order = 1;
	MPI_Comm bcast_comm;

	if(node.job_id == root) {
		if(node.node_checkpoint_master == NO) {
			color = 0;
		}
	}
	else {
		rank_order = 10;
	}

	// Is comm split an efficient method.
	// What if replica node is allowed to receive data even though it already has it.
	// Will it be more efficient as compared to creating a new comm for Bcast?

	// Found something.
	// This would require all nodes to know rank of sending node. This we have to send to all the nodes.
	// This is an overhead. Its better to split and remove replica before. Like done here.
	PMPI_Comm_split(comm, color, rank_order, &bcast_comm);

	if(color == 1) {
		return PMPI_Bcast(buffer, count, datatype, 0, bcast_comm);
	}*/
}

int MPI_Gather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm) {
	
	debug_log_i("MPI_Gather Call");
	int mpi_status = MPI_SUCCESS;
	int flag;
	int total_trails = 0;
	MPI_Comm *comm_to_use;

	is_file_update_set();
	acquire_comm_lock();

	DEFINE_BUFFER(sbuffer, sendbuf);
	DEFINE_BUFFER(rbuffer, recvbuf);
	
	if(comm == MPI_COMM_WORLD) {
		comm_to_use = &(node.rep_mpi_comm_world);
	}

	do {

		total_trails++;

		if(total_trails >= NO_TRIALS) {
			log_e("Total Trails exceeds. Aborting.");
			PMPI_Abort(node.rep_mpi_comm_world, 10);
		}

		if(node.active_comm != MPI_COMM_NULL) {
			mpi_status = PMPI_Gather(SET_RIGHT_S_BUFFER(sbuffer), sendcount, sendtype, SET_RIGHT_R_BUFFER(rbuffer), recvcount, recvtype, root, node.active_comm);
		}

		flag = (MPI_SUCCESS == mpi_status);

		// To correct the comms
		while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag)) {
			debug_log_i("First Comm agree");
			//flag = 0;
			//continue;
		}
		// To perform agree on flag
		//PMPIX_Comm_agree(comm_to_use, &flag);

		if(!flag) {
			debug_log_i("MPI_Gather Failed");
			flag = 0;
			continue;
		}
		else {

			//if(node.rank == 1)
				//raise(SIGKILL);
			
			do {

				debug_log_i("Doing Bcast");
				mpi_status = PMPI_Bcast(SET_RIGHT_R_BUFFER(rbuffer), recvcount * node.jobs_count, recvtype, 0, node.world_job_comm);
				debug_log_i("Aftter bcast: MPI_Status: %d", mpi_status == MPI_SUCCESS);
				flag = (MPI_SUCCESS == mpi_status);
				
				// To correct the comms
				while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag));
				// To perform agree on flag
				//PMPIX_Comm_agree(comm_to_use, &flag);	// TODO: Is this call even required?

				debug_log_i("[Value flag]: %d", flag);
				if(!flag) {
					debug_log_i("MPI_Bcast Failed");

					// If failed node is root node in node.world_job_comm, data is lost 
					// and main operation needs to be done again before doing bcast.
					if(is_failed_node_world_job_comm_root()) {
						debug_log_i("node.world_job_comm root node died. Doing main communication again");
						flag = 0;
						break;
					}
				}

			} while(!flag);
			
		}

		

	} while(!flag);

	release_comm_lock();

	return MPI_SUCCESS;



	/*if(node.active_comm != MPI_COMM_NULL) {
		PMPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, node.active_comm);
	}

	// TODO: if only 1 node in world_job_comm, dont do bcast.
	if(node.job_id == root) {
		PMPI_Bcast(recvbuf, recvcount * node.jobs_count, recvtype, 0, node.world_job_comm);
	}

	return MPI_SUCCESS;*/

	/*int dsize;

    if(node.job_id == root) {
    	MPI_Type_size(sendtype, &dsize);
       	for(int i=0; i<node.jobs_count; i++) {
            if(root == i)
                continue;
            MPI_Recv(recvbuf + recvcount * i * dsize, recvcount, recvtype, i, 13, comm, MPI_STATUS_IGNORE);
        }
        memcpy(recvbuf + recvcount * root * dsize, sendbuf, dsize * recvcount);
    }
    else {
        MPI_Send(sendbuf, sendcount, sendtype, root, 13, comm);
    }

    return MPI_SUCCESS;*/
    
}

int MPI_Allgather(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm) {
		
	debug_log_i("MPI_Allgather Call");
	int mpi_status = MPI_SUCCESS;
	int flag;
	int total_trails = 0;
	MPI_Comm *comm_to_use;

	is_file_update_set();
	acquire_comm_lock();

	DEFINE_BUFFER(sbuffer, sendbuf);
	DEFINE_BUFFER(rbuffer, recvbuf);

	if(comm == MPI_COMM_WORLD) {
		comm_to_use = &(node.rep_mpi_comm_world);
	}

	do {

		total_trails++;

		if(total_trails >= NO_TRIALS) {
			log_e("Total Trails exceeds. Aborting.");
			PMPI_Abort(node.rep_mpi_comm_world, 10);
		}

		if(node.active_comm != MPI_COMM_NULL) {
			mpi_status = PMPI_Allgather(SET_RIGHT_S_BUFFER(sbuffer), sendcount, sendtype, SET_RIGHT_R_BUFFER(rbuffer), recvcount, recvtype, node.active_comm);
		}

		flag = (MPI_SUCCESS == mpi_status);

		// To correct the comms
		while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag)) {
			debug_log_i("First Comm agree");
			//flag = 0;
			//continue;
		}
		// To perform agree on flag
		//PMPIX_Comm_agree(*comm_to_use, &flag);

		if(!flag) {
			debug_log_i("MPI_Allgather Failed");
			flag = 0;
			continue;
		}
		else {

			/*if(node.rank == 1)
				raise(SIGKILL);*/
			
			do {

				debug_log_i("Doing Bcast");
				mpi_status = PMPI_Bcast(SET_RIGHT_R_BUFFER(rbuffer), recvcount * node.jobs_count, recvtype, 0, node.world_job_comm);
				debug_log_i("Aftter bcast: MPI_Status: %d", mpi_status == MPI_SUCCESS);
				flag = (MPI_SUCCESS == mpi_status);
				
				// To correct the comms
				while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag));
				// To perform agree on flag
				//PMPIX_Comm_agree(comm_to_use, &flag);

				debug_log_i("[Value flag]: %d", flag);
				if(!flag) {
					debug_log_i("MPI_Bcast Failed");

					// If failed node is root node in node.world_job_comm, data is lost 
					// and main operation needs to be done again before doing bcast.
					if(is_failed_node_world_job_comm_root()) {
						debug_log_i("node.world_job_comm root node died. Doing main communication again");
						flag = 0;
						break;
					}
				}

			} while(!flag);
			
		}

		

	} while(!flag);

	release_comm_lock();

	return MPI_SUCCESS;



	/*if(node.active_comm != MPI_COMM_NULL) {
		PMPI_Allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, node.active_comm);
	}

	if(job_list[node.job_id].worker_count > 1) {
		int rank;
		PMPI_Comm_rank(node.world_job_comm, &rank);
		debug_log_i("job_list[node.job_id].worker_count: %d | Rank: %d", job_list[node.job_id].worker_count, rank);
		PMPI_Bcast(recvbuf, recvcount * node.jobs_count, recvtype, 0, node.world_job_comm);
	}
	
	return MPI_SUCCESS;*/



/*
	MPI_Comm gather_comm;
	int color = 1;
	int send_rank = node.rank;
	int sync_rank;

	if(node.node_checkpoint_master == NO) {
		color = 0;
		send_rank = 99999;
	}

	PMPI_Allreduce(&send_rank, &sync_rank, 1, MPI_INT, MPI_MIN, comm);

	PMPI_Comm_split(comm, color, 0, &gather_comm);

	if(color == 1) {
		PMPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, 0, gather_comm);
	}

	return PMPI_Bcast(recvbuf, recvcount * node.jobs_count, recvtype, sync_rank, comm);*/
}

int MPI_Reduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
	debug_log_i("In MPI_Reduce()");
	
	DECLARE_VARS()

	is_file_update_set();
	acquire_comm_lock();

	DEFINE_BUFFER(sbuffer, sendbuf);
	DEFINE_BUFFER(rbuffer, recvbuf);

	ACTIVATE_COMM_AND_BKUP_MASTER()

	do {

		TRIAL_INC_AND_CHECK()

		if(node.active_comm != MPI_COMM_NULL) {
			mpi_status = PMPI_Reduce(SET_RIGHT_S_BUFFER(sbuffer), SET_RIGHT_R_BUFFER(rbuffer), count, datatype, op, root, node.active_comm);
		}

		SET_FLAG_ON_SUCCESS()

		// To correct the comms
		// Err handler will be called even if replica node dies
		// Could be optimized by not shrinking if replica node is dead.
		DO_COMM_AGREE()

		IF_OPERATION_FAILED_ELSE("MPI_Reduce Failed", 
			
			do {
				
				if(node.job_id == root) {
					debug_log_i("Doing Bcast");
					mpi_status = PMPI_Bcast(SET_RIGHT_R_BUFFER(rbuffer), count, datatype, 0, node.world_job_comm);
					debug_log_i("Aftter bcast: MPI_Status: %d", mpi_status == MPI_SUCCESS);
				}
				else {
					mpi_status = MPI_SUCCESS;
				}
				
				SET_FLAG_ON_SUCCESS()
				
				// To correct the comms
				DO_COMM_AGREE()

				HANDLE_FAILURE_ON_JOB_COMM("MPI_Bcast Failed")

			} CONTINUE_TILL_SUCCESS();
		
		)

	} CONTINUE_TILL_SUCCESS();

	release_comm_lock();

	return MPI_SUCCESS;

	/*if(node.active_comm != MPI_COMM_NULL) {
		PMPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, node.active_comm);
		if(node.job_id == root)
			debug_log_i("OP Value: %d", *((int *)recvbuf));
	}

	if(node.job_id == root && job_list[node.job_id].worker_count > 1) {
		//int rank;
		//PMPI_Comm_rank(node.world_job_comm, &rank);
		//debug_log_i("job_list[node.job_id].worker_count: %d | Rank: %d", job_list[node.job_id].worker_count, rank);
		PMPI_Bcast(recvbuf, count, datatype, 0, node.world_job_comm);
	}
	
	return MPI_SUCCESS;*/
}

int MPI_Allreduce(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm) {
	
	debug_log_i("MPI_Allreduce Call");
	int mpi_status = MPI_SUCCESS;
	int flag;
	int total_trails = 0;
	MPI_Comm *comm_to_use;

	is_file_update_set();
	acquire_comm_lock();

	DEFINE_BUFFER(sbuffer, sendbuf);
	DEFINE_BUFFER(rbuffer, recvbuf);
	
	if(comm == MPI_COMM_WORLD) {
		comm_to_use = &(node.rep_mpi_comm_world);
	}

	do {

		total_trails++;

		if(total_trails >= NO_TRIALS) {
			log_e("Total Trails exceeds. Aborting.");
			PMPI_Abort(node.rep_mpi_comm_world, 10);
		}

		if(node.active_comm != MPI_COMM_NULL) {
			mpi_status = PMPI_Allreduce(SET_RIGHT_S_BUFFER(sbuffer), SET_RIGHT_R_BUFFER(rbuffer), count, datatype, op, node.active_comm);
		}

		flag = (MPI_SUCCESS == mpi_status);

		// To correct the comms
		while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag)) {
			debug_log_i("First Comm agree");
			//flag = 0;
			//continue;
		}
		// To perform agree on flag
		//PMPIX_Comm_agree(*comm_to_use, &flag);

		if(!flag) {
			debug_log_i("MPI_Allreduce Failed");
			flag = 0;
			continue;
		}
		else {

			/*if(node.rank == 1)
				raise(SIGKILL);*/
			
			do {

				debug_log_i("Doing Bcast");
				mpi_status = PMPI_Bcast(SET_RIGHT_R_BUFFER(rbuffer), count, datatype, 0, node.world_job_comm);
				debug_log_i("Aftter bcast: MPI_Status: %d", mpi_status == MPI_SUCCESS);
				flag = (MPI_SUCCESS == mpi_status);
				
				// To correct the comms
				while(MPI_SUCCESS != MPI_Comm_agree(*comm_to_use, &flag));
				// To perform agree on flag
				//PMPIX_Comm_agree(*comm_to_use, &flag);

				debug_log_i("[Value flag]: %d", flag);
				if(!flag) {
					debug_log_i("MPI_Bcast Failed");

					// If failed node is root node in node.world_job_comm, data is lost 
					// and main operation needs to be done again before doing bcast.
					if(is_failed_node_world_job_comm_root()) {
						debug_log_i("node.world_job_comm root node died. Doing main communication again");
						flag = 0;
						break;
					}
				}

			} while(!flag);
			
		}

		

	} while(!flag);

	release_comm_lock();

	return MPI_SUCCESS;


	/*if(node.active_comm != MPI_COMM_NULL) {
		PMPI_Allreduce(sendbuf, recvbuf, count, datatype, op, node.active_comm);
	}

	if(job_list[node.job_id].worker_count > 1) {
		// int rank;
		// PMPI_Comm_rank(node.world_job_comm, &rank);
		// debug_log_i("job_list[node.job_id].worker_count: %d | Rank: %d", job_list[node.job_id].worker_count, rank);
		PMPI_Bcast(recvbuf, count, datatype, 0, node.world_job_comm);
	}
	
	return MPI_SUCCESS;*/
}

// Non blocking MPI_I* calls

/*int MPI_Isend(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request) {
	debug_log_i("In MPI_Isend()");
	//is_file_update_set();
	acquire_comm_lock();
	debug_log_i("In MPI_Isend() after is_file_update_set");

	__ignore_process_failure = 1;

	MPI_Comm *comm_to_use;

	if(comm == MPI_COMM_WORLD) {
		// So that comm err handler do not identify it as rep_mpi_comm_world
		// It will ignore it and will not enter the if statement.
		// Note: If it enters, process will hang as functions inside are
		// collective.
		//PMPI_Comm_dup(node.rep_mpi_comm_world, &comm_to_use);

		comm_to_use = &(node.rep_mpi_comm_world);
	}

	DEFINE_BUFFER(buffer, buf);

	int mpi_status;

	Aggregate_Request *agg_req = new_agg_request(SET_RIGHT_S_BUFFER(buffer));

	// "request" variable will now contain the address of the aggregated request
	// element instead of MPI_Request.
	*request = (MPI_Request)agg_req;
	__request_pending++;

	for(int i=0; i<job_list[dest].worker_count; i++) {
		//printf("[Rank: %d] Job List: %d\n", node.rank, (job_list[dest].rank_list)[i]);
		debug_log_i("SEND: Data: %d", *((int *)buf));

		MPI_Request *r = add_new_request(agg_req);

		mpi_status = PMPI_Isend(SET_RIGHT_S_BUFFER(buffer), count, datatype, (job_list[dest].rank_list)[i], tag, *comm_to_use, r);

		if(mpi_status != MPI_SUCCESS) {
			debug_log_i("MPI_Isend Failed [Dest: %d]", (job_list[dest].rank_list)[i]);
		}
		else {
			debug_log_i("MPI_Isend Success [Dest: %d]", (job_list[dest].rank_list)[i]);
		}
	}

	__ignore_process_failure = 0;

	release_comm_lock();

	return mpi_status;
}*/

int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request) {
	debug_log_i("In MPI_Irecv()");
	//is_file_update_set();
	acquire_comm_lock();

	__ignore_process_failure = 1;

	//gdb_val = (source == node.job_id) ? 2 : 0;

	//int sender = 0;
	int mpi_status;
	int worker_count;
	MPI_Comm *comm_to_use;

	if(comm == MPI_COMM_WORLD) {
		//PMPI_Comm_dup(node.rep_mpi_comm_world, &comm_to_use);

		comm_to_use = &(node.rep_mpi_comm_world);
	}

	DEFINE_BUFFER(buffer, buf);

	if(source == MPI_ANY_SOURCE) {
		// Very tricky to set.
		// If less and user sends more MPI_Send it will block the program.
		// TODO: Find a better solution for MPI_ANY_SOURCE
		worker_count = 1;
	} else {
		worker_count = job_list[source].worker_count;
	}

	Aggregate_Request *agg_req = new_agg_request((void *)SET_RIGHT_R_BUFFER(buffer), datatype, count, worker_count);
	___temp_add = (void *)agg_req;
	// "request" variable will now contain the address of the aggregated request
	// element instead of MPI_Request.
	*request = (MPI_Request)agg_req;

	agg_req->tag = tag;

	MPI_Request *r = (MPI_Request *)malloc(sizeof(MPI_Request) * worker_count);
	agg_req->req_array = r;
	//agg_req->request_count = worker_count;

	if(source == MPI_ANY_SOURCE) {
		agg_req->async_type = ANY_RECV;
	}

	for(int i=0; i<worker_count; i++) {

		int recv_source;
		recv_source = (source == MPI_ANY_SOURCE) ? MPI_ANY_SOURCE : (job_list[source].rank_list)[i];
		/*if(source == MPI_ANY_SOURCE) {
			recv_source = MPI_ANY_SOURCE;
		} else {
			recv_source = (job_list[source].rank_list)[i];
		}*/
		agg_req->node_rank[i] = recv_source;
		if(recv_source != MPI_ANY_SOURCE && rank_ignore_list[ recv_source ] == 1) {
			*(r + i) = MPI_REQUEST_NULL;
			continue;
		}

		void *new_buf = ((char *)agg_req->temp_buffer) + i * agg_req->buffer_size;
		//MPI_Request *r = add_new_request_and_buffer(agg_req, &new_buf);
		debug_log_i("*******REQUEST BEFORE: %p | Source: %d | ori source: %d", *(r+i), recv_source, source);
		mpi_status = PMPI_Irecv( new_buf, count, datatype, recv_source, SET_TAG(recv_source, tag), *comm_to_use, r + i);
		debug_log_i("*******REQUEST AFTER: %p", *(r+i));

		if(mpi_status != MPI_SUCCESS) {
			debug_log_i("MPI_Irecv Failed [Source: %d]", recv_source);
			i--;
			//sender++;
		}
		else {
			debug_log_i("MPI_Irecv Success [Source: %d]", recv_source);
		}
	}

	__ignore_process_failure = 0;

	release_comm_lock();

	return mpi_status;
}

int MPI_Wait(MPI_Request *request, MPI_Status *status) {
	debug_log_i("In MPI_Wait()");
	acquire_comm_lock();
	__ignore_process_failure = 1;
	//void *internal_request = (0x00000000ffffffff & (unsigned)(*request));
	void *internal_request = *request;
	int s = wait_for_agg_request(internal_request, status);
	__request_pending--;
	__ignore_process_failure = 0;
	release_comm_lock();
	return s;
}

// Rewriting ULFM functions

int MPI_Comm_agree(MPI_Comm comm, int* flag) {
	if(__process_shrinking_pending) {
		int err_code = PROC_SHRINK_PENDING;
		rep_errhandler(&(node.rep_mpi_comm_world), &err_code);
		__process_shrinking_pending = 0;
		return (MPI_SUCCESS + 12);
	}

	return PMPIX_Comm_agree(comm, flag);
}
