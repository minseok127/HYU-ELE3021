#ifndef LEVELQUEUE_H
#define LEVELQUEUE_H

#include "procparse.h"

/* MLFQ Scheduler need level queue */
struct LevelQueue{
	int level;
	int timeAllotment; // Time for changing level
	int timeQuantum; // Time for Round Robin Scheduling

	struct procParse* queue[NPROC];
	int size;
	int front; // Front index of queue array
	int back; // Back index of queue array
};

void InitLevelQueue(struct LevelQueue* levelQ, 
					int level,
					int timeQuantum,
					int timeAllotment);
void PushLevelQueue(struct LevelQueue* levelQ, struct procParse* pp);
void PopLevelQueue(struct LevelQueue* levelQ);
struct procParse* GetFrontLevelQueue(struct LevelQueue* levelQ);

#endif // LEVELQUEUE_H
