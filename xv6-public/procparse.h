#ifndef PROCPARSE_H
#define PROCPARSE_H

#include "types.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "threadtypes.h"
#include "addrstack.h"

/* In the MLFQ and Stride scheduler, 
 * procParse structure is used for parsing proc structure */
struct procParse{
	struct proc* p;
	int ticket;
	double pass;
	double stride;
	uint usedTick;
	uint usedQuantumTick;
	uint lastTick;
	int level;

	unsigned short tid; // New thread's id
	struct Thread* threadNow; // Thread pointing ptable
	struct ThreadPage* threadDir[NTHREADPAGE]; // Thread directory
	struct AddrStack trashAddrStack; // To save exited thread's user stack

	int execFlag; // If one of the thread doing exec, make flag true
};

#endif // PROCPARSE_H
