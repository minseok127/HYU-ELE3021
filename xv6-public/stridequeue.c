#include "stridequeue.h"
#include "defs.h"
#include "ticketbox.h"

extern struct TicketBox ticketbox;// Total global tickets(remained tickets)
extern double CONST_FOR_STRIDE; // Used in making stride

/* Stride scheduler has time quantum
 * Declare this limit to the constants */
const int TIME_QUANTUM_STRIDE = 5;

/* Stride scheduler is initialized in the userinit() */
void InitStrideQueue(struct StrideQueue* strideQ){
	strideQ->size = 0;
}

/* Push procParse to the stride scheduler's min pass heap. */
void PushStrideQueue(struct StrideQueue* strideQ, struct procParse* pp){
	if (strideQ->size == NPROC) {
		return;
	}

	/* Use min heap algorithm, Sort procParse */
	int i = ++(strideQ->size); // Start from the last
	strideQ->minPassHeap[i] = pp;

	int parentIndex;
	struct procParse* tmp;
	while (i > 1) {
		parentIndex = i / 2;
		
		/* If child has lower pass than parent,
		 * change each other. */
		if (strideQ->minPassHeap[i]->pass
				< strideQ->minPassHeap[parentIndex]->pass) {
			tmp = strideQ->minPassHeap[parentIndex];

			strideQ->minPassHeap[parentIndex] = strideQ->minPassHeap[i];
			strideQ->minPassHeap[i] = tmp;

			i = parentIndex; // Child is moved to parent.
		}
		else {
			/* If parent has lower pass,
			 * No need to move anymore. Break the loop. */
			break;
		}
	}

}

/* Pop the top of stride scheduler's min pass heap. */
void PopStrideQueue(struct StrideQueue* strideQ){
	if (strideQ->size == 0) {
		return;
	}

	/* Use min heap algorithm */
	strideQ->minPassHeap[1] = strideQ->minPassHeap[strideQ->size];
	strideQ->size--; // Move last to the first.

	int i = 1; // Parent start from top.
	int childIndex;
	struct procParse* tmp;
	while (i * 2 + 1 <= strideQ->size) { // Parent has right child.
		/* Compare right child and left child.
		 * Then choose child who has lower pass. */
		if (strideQ->minPassHeap[i * 2]->pass
				> strideQ->minPassHeap[i * 2 + 1]->pass){
			childIndex = i * 2 + 1;
		}
		else {
			childIndex = i * 2;
		}

		/* Compare child and parent. */
		if (strideQ->minPassHeap[i]->pass
				> strideQ->minPassHeap[childIndex]->pass) {
			/* If child has lower pass,
			 * Change parent and child each other. */
			tmp = strideQ->minPassHeap[i];

			strideQ->minPassHeap[i] = strideQ->minPassHeap[childIndex];
			strideQ->minPassHeap[childIndex] = tmp;

			i = childIndex; // Parent is moved to the child
		}
		else{
			/* If parent has lower pass,
			 * No need to move anymore. Break the loop. */
			break;
		}
	}

	/* If left child is last components,
	 * this case is not managed by while loop.
	 * So, check it. */
	if (i * 2 == strideQ->size && 
		strideQ->minPassHeap[i]->pass > strideQ->minPassHeap[i * 2]->pass){
		tmp = strideQ->minPassHeap[i];

		strideQ->minPassHeap[i] = strideQ->minPassHeap[i * 2];
		strideQ->minPassHeap[i * 2] = tmp;
	}
}

/* Get the top of stride scheduler's min pass heap. */
struct procParse* GetTopStrideQueue(struct StrideQueue* strideQ){
	struct procParse* ret = 0;
	
	if (strideQ->size != 0) {
		ret =  strideQ->minPassHeap[1];
	}

	return ret;
}

/* Insert process into stride queue.
 * If ticket is not enough return -1.
 * It is called in the set_cpu_share() */
int InsertStrideQueue(struct StrideQueue* strideQ,
						  struct procParse* pp,
						  int ticket,
						  double minPass){

	acquire(&ticketbox.lock);

	/* If process already has ticket,
	 * Compare required ticket and possessed ticket */
	if (pp->ticket != 0) {
		if (pp->ticket >= ticket){
			/* If pp's ticket is larger than required ticket,
			 * give substraction back to the global tickets */
			int giveBack = pp->ticket - ticket;

			ticketbox.ticket += giveBack;
			
			pp->ticket -= giveBack;
		}
		else if(ticket - pp->ticket <= ticketbox.ticket) {
			/* If required ticket is larger than pp's ticket.
			 * and if it is possible to give required ticket */
			int require = ticket - pp->ticket;

			ticketbox.ticket -= require;

			pp->ticket += require;
		}
		else {
			/* If it is impossible to give required ticket, return -1 */
			release(&ticketbox.lock);
			return -1;
		}
	}
	/* If pp has no ticket,
	 * check it is possible to give required ticket */
	else {
		if (ticket <= ticketbox.ticket) {
			ticketbox.ticket -= ticket;
			
			/* In this case, set the pass to the lowest pass. */
			pp->ticket = ticket;
			pp->pass = minPass;
		}
		else {
			release(&ticketbox.lock);
			return -1;
		}
	}

	release(&ticketbox.lock);

	pp->stride = CONST_FOR_STRIDE / pp->ticket;
	pp->level = -1;

	// pp's pass is changed only if
	// it is inserted newly into stride queue(has no ticket at first)

	PushStrideQueue(strideQ, pp);

	return 0;
}

struct procParse* SearchStrideQueue(struct StrideQueue* strideQ){
	int originalSize = strideQ->size;

	if (originalSize == 0) {
		return 0;
	}

	struct procParse* top = 0;
	struct procParse* ret = 0;

	/* If process is alive but not runnable, save it.
	 * Then push again at last.
	 * Because if push again immediately,
	 * this process(alive but not runnable) will be chosen again. */
	struct procParse* stack[NPROC];
	int stackIndex = -1;

	/* Search runnable process */
	for (int i = 0; i < originalSize; i++) {
		top = GetTopStrideQueue(strideQ);
		PopStrideQueue(strideQ);

		/* If this process is dead or not scheduled by stride scheduler. */
		if (top->p->state == UNUSED || top->p->state == ZOMBIE
				 || top->level != -1) {
			/* Give ticket back to global ticket */
			acquire(&ticketbox.lock);
			ticketbox.ticket += top->ticket;
			release(&ticketbox.lock);

			/* Initialize top */
			top->pass = 0;
			top->stride = 0;
			top->ticket = 0;
			// Don't touch MLFQ's information.
		}
		else if (top->p->state == RUNNABLE) { // There are RUNNABLE
			ret = top;
			break;
		}
		else {
			stack[++stackIndex] = top; // Not runnable, but alive
		}
	}

	/* Push not runnable, but alive processes again
	 * Into min pass heap */
	for (int i = 0; i <= stackIndex; i++) {
		PushStrideQueue(strideQ, stack[i]);
	}

	return ret;
}
