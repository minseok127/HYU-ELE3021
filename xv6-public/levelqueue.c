#include "levelqueue.h"
#include "defs.h"

/* Level queue is initialized in InitMLFQ() */
void InitLevelQueue(struct LevelQueue* levelQ, 
					int level,
					int timeQuantum,
					int timeAllotment){
	levelQ->size = 0;
	levelQ->front = 0;
	levelQ->back = -1;

	levelQ->timeQuantum = timeQuantum;
	levelQ->timeAllotment = timeAllotment;
	levelQ->level = level;
}

/* Push procParse's address into LevelQueue */
void PushLevelQueue(struct LevelQueue* levelQ, struct procParse* pp) {
	if (levelQ->size == NPROC){
		return;
	}

	if (levelQ->back + 1 == NPROC){
		levelQ->back = 0;
	}
	else {
		levelQ->back++;
	}

	levelQ->queue[levelQ->back] = pp;
	levelQ->size++;
}

/* Pop front of LevelQueue */
void PopLevelQueue(struct LevelQueue* levelQ) {	
	if (levelQ->size == 0){
		return;
	}

	if (levelQ->front + 1 == NPROC) {
		levelQ->front = 0;
	}
	else {
		levelQ->front++;
	}

	levelQ->size--;
}

/* Get the front of LevelQueue */
struct procParse* GetFrontLevelQueue(struct LevelQueue* levelQ) {
	struct procParse* ret = 0;

	if (levelQ->size != 0) {
		ret = levelQ->queue[levelQ->front];
	}

	return ret;
}
