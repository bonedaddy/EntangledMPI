#include "comm.h"

extern Node node;

extern enum CkptBackup ckpt_backup;

int init_node(char *file_name, Job **job_list, Node *node) {
	debug_log_i("Initiating Node and Jobs data from file.");
	
	int my_rank;
	PMPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	// Remember to update this rank after migration.
	(*node).rank = my_rank;

	// If job_id = -1 not set, as "node" is a global variable and is cleared by default
	// job id 0 in the map file will not be initiated properly.
	(*node).job_id = -1;

	parse_map_file(file_name, job_list, node, &ckpt_backup);
	update_comms();

	// parse_map_file will set "node_transit_state" to "NODE_DATA_RECEIVER"
	// because it thinks all nodes are newly added to this job.
	// So a correction has to be made.
	(*node).node_transit_state = NODE_DATA_NONE;

	for(int i=0; i<(*node).jobs_count; i++) {
		debug_log_i("[Node Init] My Job ID: %d | Node Transit: %d | Job ID: %d | Worker Count: %d | Worker 1: %d | Worker 2: %d", (*node).job_id, (*node).node_transit_state, (*job_list)[i].job_id, (*job_list)[i].worker_count, (*job_list)[i].rank_list[0], (*job_list)[i].rank_list[1]);
	}
}

int parse_map_file(char *file_name, Job **job_list, Node *node, enum CkptBackup *ckpt_backup) {
	
	if(*ckpt_backup == BACKUP_YES) {
		return 0;
	}

	FILE *pointer = fopen(file_name, "r");

	if(pointer == NULL) {
		log_e("Cannot open replication map file.");
        exit(0);
	}

	int cores, jobs;
	fscanf(pointer, "%d\t%d", &cores, &jobs);

	// TODO: Optimise re-allocation of memory.
	for(int i=0; i<(*node).jobs_count; i++) {
		free((*job_list)[i].rank_list);
	}
	free(*job_list);

	int my_rank = (*node).rank;
	

	*job_list = (Job *)malloc(sizeof(Job) * jobs);
	for(int i=0; i<jobs; i++) {
		int j_id, w_c, update_bit;
		int w_rank;
		
		fscanf(pointer, "%d\t%d\t%d\t", &update_bit, &j_id, &w_c);

		assert(j_id < jobs && "Check replication map for job id > no of jobs.");
		assert(w_c > 0 && "Worker count for each job should be greater than zero.");

		if(update_bit == 0) {
			if((*node).job_id == j_id) {
				(*node).node_transit_state = NODE_DATA_NONE;
			}
			//continue;
		}

		(*job_list)[j_id].job_id = j_id;
		(*job_list)[j_id].worker_count = w_c;
		(*job_list)[j_id].rank_list = malloc(sizeof(int) * w_c);

		

		for(int j=0; j<w_c; j++) {
			
			fscanf(pointer, "\t%d", &w_rank);

			if(j == 0 && w_rank == my_rank) {
				(*node).node_checkpoint_master = YES;
			}

			if(w_rank == my_rank && update_bit == 1) {
				if((*node).job_id != j_id) {
					(*node).age = 1;
					(*node).job_id = j_id;
					(*node).node_transit_state = NODE_DATA_RECEIVER;
				}
				else {
					(*node).age++;
					// 1. Job Id same as previous but some new rank added to this job.
					// 2. First job in the map needs to send data to that rank.
					// 3. Sends data only if workers > 1. If workers == 1, then only this
					//    rank exists.
					if(j == 0 && w_c > 1) {
						(*node).node_transit_state = NODE_DATA_SENDER;	
					}
					else {
						(*node).node_transit_state = NODE_DATA_NONE;
					}
				}
				(*node).jobs_count = jobs;
				
			}
			
			(*job_list)[j_id].rank_list[j] = w_rank;
		}
	}

	for(int i=0; i<jobs; i++) {
		debug_log_i("[Rep File Update] MyJobId: %d | Job ID: %d | Worker Count: %d | Worker 1: %d | Worker 2: %d | Checkpoint: %d", (*node).job_id, (*job_list)[i].job_id, (*job_list)[i].worker_count, (*job_list)[i].rank_list[0], (*job_list)[i].rank_list[1], (*node).node_checkpoint_master);
	}
}

// responsible to update 'world_job_comm' and 'active_comm' [defined in src/shared.h]
// 'world_job_comm': Communicator all all nodes in a job.
// 'active_comm': Communicator of nodes, one from each job. So these can be called active nodes.
void update_comms(int comm_key_world, int comm_key_job_world, int comm_key_active_nodes) {
	int color = 0, rank_key = node.job_id;

	// Although misguiding 'node.node_checkpoint_master' is not just used to mark a node
	// which takes checkpoint on behalf of a job but it is also used to do communications
	// amoung other jobs. Then the result is send to all nodes of 'this' job.
	
	// 'node.node_checkpoint_master == NO' are the nodes not responsible for checkpointing
	// in this job.
	if(node.node_checkpoint_master == NO) {
		color = MPI_UNDEFINED;
	}

	PMPI_Comm_split(MPI_COMM_WORLD, color, rank_key, &(node.active_comm));

	// Test
	int rank;

	if(node.active_comm != MPI_COMM_NULL) {
		PMPI_Comm_rank(node.active_comm, &rank);
		debug_log_i("Job ID: %d | active_comm Rank: %d", node.job_id, rank);
	}
	

	color = node.job_id;
	if(node.node_checkpoint_master == YES) {
		rank_key = 0;
	}
	else {
		rank_key = 1;
	}

	PMPI_Comm_split(MPI_COMM_WORLD, color, rank_key, &(node.world_job_comm));

	// Test
	PMPI_Comm_rank(node.world_job_comm, &rank);
	debug_log_i("Job ID: %d | world_job_comm Rank: %d", node.job_id, rank);
}

/* Returns 1 if comm is valid on this node, else 0. */
int create_migration_comm(MPI_Comm *job_comm, int *rep_flag, enum CkptBackup *ckpt_backup) {
	/* 								this^
	*  This comm will contain all the processes which are either sending or receiving 
	*  replication data. 
	*/
	int color, key, flag;

	if(*ckpt_backup == BACKUP_YES) {
		return 1;
		*rep_flag = 0;
	}

	if(node.node_transit_state != NODE_DATA_NONE) {
		color = node.job_id;
		if(node.node_transit_state == NODE_DATA_SENDER) {
			key = 0;
		}
		else {
			key = 1;
		}
		flag = 1;
		debug_log_i("Comm Created. | Job: %d", node.job_id);
	}
	else {
		color = MPI_UNDEFINED;
		flag = 0;
		debug_log_i("No Comm Created. | Job: %d", node.job_id);
	}

	*rep_flag = flag;

	debug_log_i("Color: %d | key: %d | job_comm: %p", color, key, job_comm);

	PMPI_Comm_split(MPI_COMM_WORLD, color, key, job_comm);

	debug_log_i("Create Migration Comm: flag: %d | ckpt master: %d", flag, node.node_checkpoint_master);
	
	return (flag || node.node_checkpoint_master);
}
