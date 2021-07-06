#ifndef THREAD_H
#define THREAD_H

#include "procparse.h"

struct ThreadId ForkThread(struct procParse* pp, 
						  void* (*start_routine)(void*),
						  void* arg);

struct Thread* AllocThread(struct procParse* pp);

int SetUstack(struct procParse* pp, struct Thread* t, void* arg);

struct proc* swap(struct procParse* pp, int state);

int FreeThread(struct procParse* pp, struct Thread* target);

#endif // THREAD_H
