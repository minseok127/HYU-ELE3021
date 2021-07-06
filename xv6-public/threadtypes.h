#ifndef THREADTYPES_H
#define THREADTYPES_H

#define NTHREAD (26) // Thread's size is 152 bytes.
#define NTHREADPAGE (10)

struct ThreadId {
	short pageNum;
	unsigned short tid;
};

struct Thread {
	struct proc* p;
	struct proc lwp;
	short pageNum;
	unsigned short tid;
	struct Thread* next; // Used join tree
	struct Thread* head; // Used join tree
	struct Thread* tail; // Used join tree
	void* retval;
	uint ustackTop; // user stack's top address
};

struct ThreadPage {
	int threadNum;
	struct Thread threadArr[NTHREAD];
};

#endif // THREADTYPES_H
