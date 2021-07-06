#include "thread.h"
#include "defs.h"
#include "x86.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

extern void forkret(void);
extern void trapret(void);

extern uint ticks; // for debugging

struct Thread*
AllocThread(struct procParse* pp)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  // tid is unsigned short
  // If required tid is more than 65535, 
  // reset tid to 1

  if (pp->tid == 65535) {
  	pp->tid = 1; // 0 is main thread's id
  }

  unsigned short index = pp->tid % NTHREAD; // New thread's index

  /* Search empty room in the page directory
   * For new thread */
  struct ThreadPage* pg = 0;
  struct Thread* nt = 0;
  for (int i = 0; i < NTHREADPAGE; i++) {
  		pg = pp->threadDir[i]; // Page i
		
		if (pg == 0) {
			// If there are no page, allocate new page
			if ((pg = (struct ThreadPage*)kalloc()) == 0) {
				release(&ptable.lock);
				cprintf("AllocThread err: kalloc failed\n");
				return 0;
			}

			pp->threadDir[i] = pg; // Push page into directory
			memset(pg, 0, PGSIZE); // Initialize to 0
		}

		// If page's index is empty, allocate it to the new thread
		if (pg->threadArr[index].p == 0) {
			nt = &(pg->threadArr[index]); // nt : new thread
			nt->pageNum = i; // Set new thread's pagenum info
			goto found;
		}
  }

  release(&ptable.lock);
  return 0;

found:
  nt->p = &(nt->lwp); // new thread point proc in the thread
  nt->tid = pp->tid++; // Set new thread's tid info
  memmove(nt->p, pp->p, sizeof(struct proc)); // copy ptable's proc

  p = nt->p; // Inner proc
  p->state = EMBRYO;
  pg->threadNum++; // page get new thread

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    memset(nt, 0, sizeof(struct Thread));
	cprintf("AllocThread err: allocating kernel stack failed\n");
    return 0;
  }
  sp = p->kstack + KSTACKSIZE; // make sp to the top of kernel stack

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return nt;
}

struct ThreadId
ForkThread(struct procParse* pp, void*(*start_routine)(void*), void* arg)
{
  /* retId initialzied by -1. no id */
  struct ThreadId retId;
  retId.pageNum = -1;
  retId.tid = -1;

  struct Thread* newThread;
  struct proc* curproc = pp->p; // pp->p is ptable's proc

  // Allocate thread.
  if((newThread = AllocThread(pp)) == 0){
	cprintf("ForkThread err: AllocThread failed\n");
    return retId; // -1 id return
  }

  struct proc* np = newThread->p; // newThread->p is proc inside Thread

  // Copy trap frame
  *np->tf = *curproc->tf;

  // Clear %eax, Set start routine
  //np->tf->eax = 0;
  np->tf->eip = (uint)start_routine;

  // Set file info
  for(int i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = curproc->ofile[i];
  np->cwd = curproc->cwd;

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  // Set user stack
  // If pp has trash address, recycle it
  acquire(&ptable.lock);

  uint top, sp;
  // If pp has trash address, recycle it
  // cf) address stack has bottom of trash stack. Not a top of trash stack
  // => this 'top' variable has the top of user stack after allocuvm. 
  // before allocuvm, it is not a top
  if ((top = GetTopAddrStack(&pp->trashAddrStack)) != 0) {
    PopAddrStack(&pp->trashAddrStack);

	// Using trash address, make new user stack
  	if((top = allocuvm(curproc->pgdir, top, top + 2 * PGSIZE)) == 0) {
		release(&ptable.lock);	
		cprintf("ForkThread err: allocuvm is 0\n");
		return retId;
	}
	clearpteu(curproc->pgdir, (char*)(top - 2 * PGSIZE));
	sp = top;

	uint ustack[2];
	ustack[0] = 0xffffffff;
	ustack[1] = (uint)arg;
	sp -= 2 * 4;

	if (copyout(curproc->pgdir, sp, ustack, 2 * 4) < 0) {
		release(&ptable.lock);
		cprintf("ForkThread err: copyout failed\n");
		return retId;
	}

  	// Commit to the user image.
	// Do not touch size info.
  	newThread->p->tf->esp = sp; // Set user stack pointer
  }
  // If there are no trash address,
  // Allocate new user memory for making new user stack
  else if ((top = SetUstack(pp, newThread, arg)) == -1) {
  	release(&ptable.lock);
	cprintf("ForkThread err: SetUstack failed\n");
	return retId;
  }

  newThread->ustackTop = top; // Save user stack's top

  np->state = RUNNABLE;

  release(&ptable.lock);

  // Return thread id that has thread's location info
  retId.pageNum = newThread->pageNum;
  retId.tid = newThread->tid;

  return retId;
}

int
SetUstack(struct procParse* pp, struct Thread* t, void* arg)
{
  struct proc* curproc = pp->p;
  uint sz, sp, ustack[2]; // start_routine's argument, fake ret addr
  pde_t *pgdir = curproc->pgdir;
  sz = curproc->sz;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0) {
    cprintf("SetUstack err: allocuvm failed\n");  
  	return -1;
  }
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push start routine, arg
  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = (uint)arg;

  // Move ustack's data into real user stack
  sp -= 2 * 4;
  if(copyout(pgdir, sp, ustack, 2 *4) < 0) {
	cprintf("SetUstack err: copyout failed\n");
  	return -1;
  }

  // Commit to the user image.
  curproc->sz = sz; // Set new size of process memory
  t->p->sz = sz; // Make sz same with ptable's proc
  t->p->tf->esp = sp; // Set trapframe's stack pointer to user stack

  return sz; // return top of user stack
}

// Find thread who has 'state'
// and change ptable's proc to that thread's proc
// and return original lwp inside of previous thread
// If there are no thread who has 'state', do not change. just return 0
struct proc*
swap(struct procParse* pp, int state)
{
	if (pp == 0) {
		return 0;
	}

	struct proc* p = pp->p; // get proc at ptable
	uint sz = p->sz; // save sz from ptable's proc

	/* Save ptable's proc into thread structure */
	struct Thread* curthread = pp->threadNow;
	memmove(&(curthread->lwp), p, sizeof(struct proc));
	
	// Disconnect thread with ptable's proc
	// Reconnect with inner proc
	curthread->p = &(curthread->lwp);
	
	/* Search lwp who has 'state'
	 * after current thread's id */
	struct ThreadPage* pg = 0;
	struct Thread* target = 0;
	int pn = curthread->pageNum;
	unsigned int i = (curthread->tid % NTHREAD) + 1;

	// Search lwp who has 'state'
	for (; pn < NTHREADPAGE; pn++) {
		pg = pp->threadDir[pn];

		if (pg == 0) {
			continue;
		}
	
		for (; i < NTHREAD; i++) {
			target = &(pg->threadArr[i]);
			
			if (target->p != 0 && target->p->state == state) {
				/* Connect target thread's proc with ptable */
				memmove(p, &(target->lwp), sizeof(struct proc));
				p->sz = sz; // Set last sz
				target->p = p; // thread points ptable's proc
				pp->threadNow = target;
				return &(curthread->lwp); // Return original lwp
			}
		}
		
		// Read all thread in this page. reset i to 0
		i = 0;
	}

	/* If serach failed, start from 0 to current thread */
	for (pn = 0; pn <= curthread->pageNum; pn++) {
		pg = pp->threadDir[pn];

		if (pg == 0) {
			continue;
		}
		
		for(i = 0; i < NTHREAD; i++) {
			target = &(pg->threadArr[i]);

			if (target->p != 0 && target->p->state == state) {
				/* Connect target with ptable */
				memmove(p, &(target->lwp), sizeof(struct proc));
				p->sz = sz; // Set last size
				target->p = p; // thread points ptable's proc
				pp->threadNow = target;
				return &(curthread->lwp);
			}
		}
	}

	// There are no thread who has 'state'
	// Reconnect current thread with ptable
	curthread->p = p;

	return 0;
}

// At last of join, Remove joining thread
// Initialize that place to 0
int
FreeThread(struct procParse* pp, struct Thread* target) {
	struct proc* curproc = myproc(); // ptable's proc
	struct proc* p = target->p; // proc inside of thread structure
	if (target->p == curproc) {
		panic("FreeThread err: removing target is ptable's proc");
	}

	uint ustackTop = target->ustackTop; // Top of user stack(lwp)

	// To check page's thread number
	struct ThreadPage* pg = 0;
	int pn = target->pageNum;
	
	// Only remove zombie thread
	if(p->state != ZOMBIE){
		cprintf("FreeThread err: target is not ZOMBIE\n");
		return -1;
	}

	// free kernel stack
	kfree(p->kstack);

	// Make this place to 0
	memset(target, 0, sizeof(struct Thread));

	// If this page has no thread, free it
	pg = pp->threadDir[pn];
	if (--pg->threadNum == 0) {
		pp->threadDir[pn] = 0;
		kfree((char*)pg);
	}

	// Save user stack's bottom for recycling
	uint bot;
	bot = deallocuvm(curproc->pgdir, ustackTop, ustackTop - 2 * PGSIZE);
	PushAddrStack(&pp->trashAddrStack, bot);

	// Reload cr3 for resetting TLB register
	switchuvm(curproc);

	return 0;
}


