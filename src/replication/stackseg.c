#include "stackseg.h"

#include "src/shared.h"
#include "src/replication/rep.h"

/* 
* [Need an update: now using *argc to get higher end of stack]
* Stack Migration:
*  	If argc has value (decimal) 01 => (bits) 00000000 00000000 00000000 00000001 => (hex) 00 00 00 01
*  				0000	|________|
*				0001	|________|
*  				0002 UB	|________|
*				0003	|________|
*  				....	|........|
*				1005	|________|
*				1006	|________|	
*				1007	|___01___|	<- &argc (In little endian format)
*				1008	|___00___|
*				1009	|___00___|
*	main ->		100a LB	|___00___|  <- rbp (rbp might not be used after optimisation)
*				100b	|________|
*				100c	|________|
*
* How to migrate stack?
*  	Identify stack address from where main function starts. This will be the upper bound
*  	ie. higher address (as stack grows higher to lower address). Now identify lower bound
*  	this is the address of the most deep function currently in stack.
*
* Idetifying from where main function starts.
*  	Use address of main functions arguments ie. &argc. argc is defined relative to $rbp
*	or $rsp and is the first thing in main function stack frame.
*
* NOTE: Remember to add 3 bytes to &argc to get Lower Bound (LB)
*
*/

extern address stackHigherAddress;
extern address stackLowerAddress;

extern Node node;
extern jmp_buf context;

int transfer_stack_seg(MPI_Comm job_comm) {
	log_i("Stack Seg Transfer Init.");
	

	int stack_size;
	Context processContext;
	if(node.node_transit_state == NODE_DATA_SENDER) {
		stackLowerAddress = getRSP(context);

		debug_log_i("[SENDER] [Stack] Lower Address: %p | Higher Address: %p", stackLowerAddress, stackHigherAddress);

		stack_size = get_stack_size();

		processContext.rip = getPC(context);
		processContext.rsp = getRSP(context);
		processContext.rbp = getRBP(context);

		debug_log_i("[SENDER] PC: %p | RSP: %p | RBP: %p", processContext.rip, processContext.rsp, processContext.rbp);
	}

	PMPI_Bcast(&processContext, sizeof(Context), MPI_BYTE, 0, job_comm);

	//printf("Rank: %d | PC: %p | RSP: %p | RBP: %p\n", node.rank, processContext.rip, processContext.rsp, processContext.rbp);

	PMPI_Bcast(&stack_size, 1, MPI_INT, 0, job_comm);

	if(node.node_transit_state == NODE_DATA_RECEIVER) {
		setPC(context, processContext.rip);
		setRSP(context, processContext.rsp);
		setRBP(context, processContext.rbp);

		stackLowerAddress = stackHigherAddress - (stack_size - 4); 
	}

	address stackLAddComp = stackLowerAddress;

	PMPI_Bcast(&stackLAddComp, sizeof(address), MPI_BYTE, 0, job_comm);

	if(stackLAddComp != stackLowerAddress) {
		log_e("Compute and Replica Process Don not have same memory map. Stack High address differs");
		MPI_Abort(node.rep_mpi_comm_world, 1);
	}

	debug_log_i("StackLowerAddress: %p | StackHigerAddress: %p", stackLowerAddress, stackHigherAddress);

	PMPI_Bcast((void *)stackLowerAddress, stack_size, MPI_BYTE, 0, job_comm);

	log_i("Stack Seg Transfer Ended.");
}

int get_stack_size() {
	// adding 5: 1 for boundry element, 3 for: Remember to add 4 bytes to &argc to get Lower Bound (LB)
	int stack_size = ((char *)stackHigherAddress - (char *)stackLowerAddress) + 4;	

	debug_log_i("Stack Size: %d", stack_size);

	return stack_size;
}
