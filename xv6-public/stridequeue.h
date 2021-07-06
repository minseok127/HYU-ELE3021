#ifndef STRIDEQUEUE_H
#define STRIDEQUEUE_H

#include "procparse.h"
#include "param.h"

struct StrideQueue{
	struct procParse* minPassHeap[NPROC + 1]; // Not use 0 index
	int size;
};

void InitStrideQueue(struct StrideQueue* strideQ);

void PushStrideQueue(struct StrideQueue* strideQ, struct procParse* pp);
void PopStrideQueue(struct StrideQueue* strideQ);
struct procParse* GetTopStrideQueue(struct StrideQueue* strideQ);

int InsertStrideQueue(struct StrideQueue* strideQ,
						  struct procParse* pp,
						  int ticket,
						  double minPass);

struct procParse* SearchStrideQueue(struct StrideQueue* strideQ);

#endif // STRIDEQUEUE_H
