#include "mlfq.h"
#include "defs.h"
#include "ticketbox.h"

extern struct TicketBox ticketbox;// Total global tickets(remained tickets)
extern double CONST_FOR_STRIDE; // Used in making stride

/* MLFQ has time limits for scheduling process.
 * Declare these limits to the constants. */
const int TIME_QUANTUM_LEVEL0 = 5;
const int TIME_QUANTUM_LEVEL1 = 10;
const int TIME_QUANTUM_LEVEL2 = 20;

const int TIME_ALLOT_LEVEL0 = 20;
const int TIME_ALLOT_LEVEL1 = 40;
const int TIME_ALLOT_LEVEL2 = -1;
// Level 2 is lowest level. It's time allotment has no limit.
// So, set the tiem allotment into -1 to distinguish.

/* MLFQ has boosting period */
const int BOOSTING_PERIOD = 200;

extern struct spinlock ticketLock;

/* MLFQ is initialized in the userinit() */
void InitMLFQ(struct MLFQ* mlfq, int ticket){
	acquire(&ticketbox.lock);
	ticketbox.ticket -= ticket;
	release(&ticketbox.lock);

	mlfq->size = 0;
	mlfq->stride = CONST_FOR_STRIDE / ticket;
	mlfq->pass = 0;
	mlfq->usedTick = 0;

	InitLevelQueue(&mlfq->qLevel0, 0,
					TIME_QUANTUM_LEVEL0, TIME_ALLOT_LEVEL0);

	InitLevelQueue(&mlfq->qLevel1, 1,
					TIME_QUANTUM_LEVEL1, TIME_ALLOT_LEVEL1);
	
	InitLevelQueue(&mlfq->qLevel2, 2,
					TIME_QUANTUM_LEVEL2, TIME_ALLOT_LEVEL2);
}

struct procParse* SearchMLFQ(struct MLFQ* mlfq){
	struct procParse* pp = 0;
	struct procParse* ret = 0;
	
	/* If MLFQ used 100 ticks, Boost all processes */
	if (mlfq->usedTick >= BOOSTING_PERIOD) {
		BoostMLFQ(mlfq);
		mlfq->usedTick = 0;
	}

	/* If qlevel0 has process, select the runnable process */
	if (mlfq->qLevel0.size != 0) {
		pp = GetFrontLevelQueue(&mlfq->qLevel0);

		/* If front process already used time quantum,
		 * Move it to same level's back */
		if (mlfq->qLevel0.timeQuantum <= pp->usedQuantumTick) {
			PopLevelQueue(&mlfq->qLevel0);
			PushLevelQueue(&mlfq->qLevel0, pp);
		
			/* Initialize pp's used quanutm tick */
			pp->usedQuantumTick = 0;
		}

		/* Search runnable process.
		 * In the loop, level queue's size can be changed.
		 * So, save original size to possible to check all components */
		int originalSize = mlfq->qLevel0.size;
		for (int i = 0; i < originalSize; i++) {
			pp = GetFrontLevelQueue(&mlfq->qLevel0);

			/* If front process is not selectable,
			 * Pop it. No push again */
			if (pp->level == -1 || // It is managed by stride scheduler.
				pp->p->state == UNUSED ||
				pp->p->state == ZOMBIE) {
				PopLevelQueue(&mlfq->qLevel0);
				mlfq->size--;
			}
			/* Else if front process already used time allotment,
			 * Move it to low level */
			else if (pp->usedTick >= mlfq->qLevel0.timeAllotment) {
				PopLevelQueue(&mlfq->qLevel0);
				PushLevelQueue(&mlfq->qLevel1, pp);
		
				/* Initialize process's used tick */
				pp->usedTick = 0;
				pp->usedQuantumTick = 0;
				pp->level = 1; // Change the level
			}
			/* Else if process is runnable, select it. */
			else if (pp->p->state == RUNNABLE) {
				ret = pp;
				break;
			}
			/* Else this process is not runnable,
			 * just pop and push again */
			else {
				PopLevelQueue(&mlfq->qLevel0);
				PushLevelQueue(&mlfq->qLevel0, pp);
			}
		}
	}
	
	/* Even if qlevel0 has process, 
	 * It is possible there are no runnable process.
	 * In this case, check the qlevel1 to select the runnable process.
	 * So, use "if". Not "else if" */
	if (ret == 0 && mlfq->qLevel1.size != 0) {
		pp = GetFrontLevelQueue(&mlfq->qLevel1);
		/* If process already used time quantum,
		 * Move it to same level's back */
		if (mlfq->qLevel1.timeQuantum <= pp->usedQuantumTick) {
			PopLevelQueue(&mlfq->qLevel1);
			PushLevelQueue(&mlfq->qLevel1, pp);
		
			/* Initialize process's used quanutm tick */
			pp->usedQuantumTick = 0;
		}

		/* Search runnable process.
		 * In the loop, level queue's size can be changed.
		 * So, save original size to possible to check all components */
		int originalSize = mlfq->qLevel1.size;
		for (int i = 0; i < originalSize; i++) {
			pp = GetFrontLevelQueue(&mlfq->qLevel1);

			/* If front process is not selectable,
			 * Pop it. No push again */
			if (pp->level == -1 ||
				pp->p->state == UNUSED ||
				pp->p->state == ZOMBIE) {
				PopLevelQueue(&mlfq->qLevel1);	
				mlfq->size--;
			}
			/* Else if front process already used time allotment,
			 * Move it to low level */
			else if (pp->usedTick >= mlfq->qLevel1.timeAllotment) {
				PopLevelQueue(&mlfq->qLevel1);
				PushLevelQueue(&mlfq->qLevel2, pp);
		
				/* Initialize process's used tick */
				pp->usedTick = 0;
				pp->usedQuantumTick = 0;
				pp->level = 2; // Change the level
			}
			/* Else if process is runnable, select it. */
			else if (pp->p->state == RUNNABLE) {
				ret = pp;
				break;
			}
			/* Else this process is not runnable,
			 * just pop and push again */
			else {
				PopLevelQueue(&mlfq->qLevel1);
				PushLevelQueue(&mlfq->qLevel1, pp);
			}
		}

	}

	/* With the same reason, use "if". Not "else if" */
	if (ret == 0 && mlfq->qLevel2.size != 0) {
		pp = GetFrontLevelQueue(&mlfq->qLevel2);
		/* If process already used time quantum,
		 * Move it to same level's back */
		if (mlfq->qLevel2.timeQuantum <= pp->usedQuantumTick) {
			PopLevelQueue(&mlfq->qLevel2);
			PushLevelQueue(&mlfq->qLevel2, pp);
		
			/* Initialize process's used quanutm tick */
			pp->usedQuantumTick = 0;
		}

		/* Level2 is lowest level. So it has no limited time allotment.
		 * So, just check time quantum only. */

		/* Search runnable process. 
		 * In the loop, level queue's size can be changed.
		 * So, save original size to possible to check all components */
		int originalSize = mlfq->qLevel2.size;
		for (int i = 0; i < originalSize; i++) {
			pp = GetFrontLevelQueue(&mlfq->qLevel2);

			/* If front process is not selectable,
			 * Pop it. No push again */
			if (pp->level ==  -1 ||
				pp->p->state == UNUSED ||
				pp->p->state == ZOMBIE) {
				PopLevelQueue(&mlfq->qLevel2);
				mlfq->size--;
			}
			/* Else if process is runnable, select it. */
			else if (pp->p->state == RUNNABLE) {
				ret = pp;
				break;
			}
			/* Else this process is not runnable,
			 * just pop and push again */
			else {
				PopLevelQueue(&mlfq->qLevel2);
				PushLevelQueue(&mlfq->qLevel2, pp);
			}
		}	
	}

	return ret;
}

/* Boost all process's level to highest level.
 * Lowest level queue's processes may be more starved.
 * So, the lower level, make it the higher priroriry. */
void BoostMLFQ(struct MLFQ* mlfq){
	struct LevelQueue tmpQ; // qLevel0 will be saved at this queue.
	tmpQ.size = 0;
	tmpQ.front = 0;
	tmpQ.back = -1;

	/* Save highest queue */
	int originalSize = mlfq->qLevel0.size;
	struct procParse* target;
	for (int i = 0; i < originalSize; i++) {
		target = GetFrontLevelQueue(&mlfq->qLevel0);
		PopLevelQueue(&mlfq->qLevel0);
		PushLevelQueue(&tmpQ, target); // Push to the tmpQ
	}

	/* Move low level queue to highest level queue */
	originalSize = mlfq->qLevel2.size; // lowest level queue
	for (int i = 0; i < originalSize; i++) {
		target = GetFrontLevelQueue(&mlfq->qLevel2);

		/* Initialize tick information */
		target->usedTick = 0;
		target->usedQuantumTick = 0;
		target->level = 0; // Change level to highest level

		PopLevelQueue(&mlfq->qLevel2);
		PushLevelQueue(&mlfq->qLevel0, target); // Push to the level0
	}

	originalSize = mlfq->qLevel1.size; // Middle level queue
	for (int i = 0; i <originalSize; i++) {
		target = GetFrontLevelQueue(&mlfq->qLevel1);
		
		/* Initialize tick information */
		target->usedTick = 0;
		target->usedQuantumTick = 0;
		target->level = 0; // Change level to highest level
		
		PopLevelQueue(&mlfq->qLevel1);
		PushLevelQueue(&mlfq->qLevel0, target); // Push to the level0
	}

	originalSize = tmpQ.size; // tmpQ's components was highest level queue
	for (int i = 0; i < originalSize; i++) {
		target = GetFrontLevelQueue(&tmpQ);
		
		/* Initialize tick information */
		target->usedTick = 0;
		target->usedQuantumTick = 0;
		target->level = 0;
		
		PopLevelQueue(&tmpQ);
		PushLevelQueue(&mlfq->qLevel0, target); // Push to the level0
	}	
}

void InsertMLFQ(struct MLFQ* mlfq, struct procParse* pp){
	pp->level = 0;
	pp->usedTick = 0;
	pp->usedQuantumTick = 0;
	pp->stride = 0;
	pp->pass = 0;
	pp->ticket = 0;

	
	PushLevelQueue(&mlfq->qLevel0, pp);
	mlfq->size++;
}

