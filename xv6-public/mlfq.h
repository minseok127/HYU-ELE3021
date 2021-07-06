#ifndef MLFQ_H
#define MLFQ_H

#include "levelqueue.h"

/* Multi-Level-Feedback-Queue
 * MLFQ will be combined with Stride scheduling.
 * So, MLFQ has stride */
struct MLFQ {
	double pass;
	double stride;
	unsigned int usedTick;
	int size;

	/* It has 3 level queue */
	struct LevelQueue qLevel0;
	struct LevelQueue qLevel1;
	struct LevelQueue qLevel2;
};

/* Initialize MLFQ */
void InitMLFQ(struct MLFQ* mlfq, int ticket); 

/* Return selected process's address.
 * If there are no selected process, return null. */
struct procParse* SearchMLFQ(struct MLFQ* mlfq);

void BoostMLFQ(struct MLFQ* mlfq); // Priority Boosting

void InsertMLFQ(struct MLFQ* mlfq, struct procParse* pp);

#endif // MLFQ_H
