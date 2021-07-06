#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "mlfq.h"
#include "stridequeue.h"
#include "thread.h"
#include "ticketbox.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

/* Information for stride */
struct TicketBox ticketbox;
const double CONST_FOR_STRIDE = 0.1;

/* MLFQ */
struct MLFQ mlfq;
const int MINIMUM_TICKET_MLFQ = 20;

/* Stride scheduler */
struct StrideQueue strideQ;

/* procParse table */
struct procParse ppTable[NPROC];

struct procParse* schedule(void); // To select the runnable process

/* Time quantums for scheduling */
extern int TIME_QUANTUM_STRIDE;
extern int TIME_QUANTUM_LEVEL0;
extern int TIME_QUANTUM_LEVEL1;
extern int TIME_QUANTUM_LEVEL2;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unkown apicid\n");
}

// Disable interrpts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
	struct cpu *c;
	struct proc *p;
	pushcli();
	c = mycpu();
	p = c->proc;
	popcli();
	return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  int pIndex = 0; // Used in mapping proc and procParse
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if(p->state == UNUSED)
			goto found;
		pIndex++;
  }

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

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

  // Make main thread
  struct procParse* pp = ppTable + pIndex;
  struct Thread* t = AllocThread(pp);

  /* Insert new process into MLFQ */
  // and set thread info
  acquire(&ptable.lock);
  InsertMLFQ(&mlfq, pp);
  pp->threadNow = t;
  t->p = p;
  release(&ptable.lock);

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  //Before start processing, initialize scheduler.

  /* Initialize ppTable using ptable
   * mapping proc with procParse, and set values to 0.
   * ps. We need  proc's address only. So locking is not necessary.*/
  int i = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
  	ppTable[i].p = p;
	ppTable[i].ticket = 0;
	ppTable[i].pass = 0;
	ppTable[i].stride = 0;
	ppTable[i].usedTick = 0;
	ppTable[i].usedQuantumTick = 0;
	ppTable[i].lastTick = 0;
	ppTable[i].level = -1;
	ppTable[i].tid = 0;
	
	for (int pn = 0; pn < NTHREADPAGE; pn++) {
		ppTable[i].threadDir[pn] = 0;
	}

	InitAddrStack(&ppTable[i].trashAddrStack);

	ppTable[i].execFlag = 0;

	i++;
  }	

  // Initialize ticketbox
  ticketbox.ticket = 100;

  /* Initialize schedulers */
  InitMLFQ(&mlfq, MINIMUM_TICKET_MLFQ);
  InitStrideQueue(&strideQ);

  p = allocproc();
    
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
 
  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  struct procParse* curpp = ppTable + (curproc - ptable.proc);
  struct procParse* newpp; // new process's procparse

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  newpp = ppTable + (np - ptable.proc);

  // Copy process state from proc.
  // Current process's size can be changed by other threads
  // So must be protected
  acquire(&ptable.lock);

  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
	kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
	release(&ptable.lock);
    return -1;
  }

  // new process must have the same trash address stack
  // copy it
  newpp->trashAddrStack.size = curpp->trashAddrStack.size;
  newpp->trashAddrStack.top = curpp->trashAddrStack.top;
  for (int i = 0; i < SIZE_ADDRSTACK; i++) {
  	newpp->trashAddrStack.arr[i] = curpp->trashAddrStack.arr[i];
  }

  np->sz = curproc->sz;
  
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  // Before jumping, Make all lwps to the ZOMBIE
  struct procParse* pp = ppTable + (curproc - ptable.proc);
  struct ThreadPage* pg = 0;
  struct Thread* t = 0;
  for (int pn = 0; pn < NTHREADPAGE; pn++) {
      pg = pp->threadDir[pn];
	  
	  if (pg == 0) {
	  	continue;
	  }

	  for (int i = 0; i < NTHREAD; i++) {
	      t = &(pg->threadArr[i]);

		  if (t->p != 0) {
		      t->p->state = ZOMBIE;
		  }
	  }
  }

  // Even if this process scheduled by stride queue,
  // It is no necessary to push again into stride queue.
  // Because this process call exit, all lwp is ZOMBIE
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
		struct procParse* pp = ppTable + (p - ptable.proc);
  
  		// Initialize address stack
  		int size = pp->trashAddrStack.size;
  		for(int i = 0; i < size; i++) {
  			PopAddrStack(&pp->trashAddrStack);
  		}

  		// Remove thread directory
		// user stack will be deallocated at freevm()
  		struct ThreadPage* pg = 0;
  		struct Thread* t = 0;
		for(int pn = 0; pn < NTHREADPAGE; pn++) {
  			pg = pp->threadDir[pn];

			if (pg != 0) {
				// Check threads
				// If threads has kernel stack, free it
				for (int i = 0; i < NTHREAD; i++) {
					t = &(pg->threadArr[i]);

					// Do not touch ptable's proc
					// ptable's kernel stack will be deallocated
					// after this loop
					if (t->p == &(t->lwp) && t->p->kstack != 0) {
						kfree((char*)t->p->kstack);
					}
				}
				
				// Free thread page
				kfree((char*)pg);
				pp->threadDir[pn] = 0;
			}
 		}

  		// Initailize tid
  		pp->tid = 0;

		// make exec flag 0
		pp->execFlag = 0;

        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;

  struct procParse* pp = 0;
  struct proc* p = 0;
	
  for(;;){
	// Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    
	// It is possible that ptable's proc is SLEEPING
	// but another lwp in the same process can be RUNNABLE.
	// Upload RUNNABLE lwp to the ptable's proc
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if (p->state == UNUSED) {
			continue;
		}

		pp = ppTable + (p - ptable.proc);
		swap(pp, RUNNABLE); // swap ptable's proc to the RUNNABLE lwp
	}

	pp = schedule();

	if (pp != 0) {	
		c->proc = pp->p;

		switchuvm(c->proc);

		c->proc->state = RUNNING;

		swtch(&(c->scheduler), c->proc->context);

		switchkvm();

		pp = ppTable + (c->proc - ptable.proc);

		// If process that switched into scheduler is ZOMBIE,
		// It means that there are no RUNNALBE lwp in the same group.
		// Because if there are RUNNALBE lwp, 
		// switching between scheduler is not possible.
		// So, two case is possible
		
		// 1. SLEEPING lwp exists
		// In this case, ptable's proc must be replaced to SLEEPING lwp
		// If ptable's proc is not replaced, scheduler will
		// remove this process because ptable's proc is ZOMBIE
		
		// 2. No SLEEPING lwp
		// In this case, process's all lwps are ZOMBIE
		// this process will be removed by scheudler

		// cf. switching between ZOMBIE proc in ptable
		// with SLEEPING proc in other thread's lwp 
		// must be occured in scheduler.
		// If this occur in ZOMBIE's context(e.g sched2)
		// SLEEPING proc will have ZOMBIE'S context
		// (More detail)
		// ptable <-----------------> thread 1 (ZOMBIE)
		//                            thread 2 (SLEEPING)
		//                   
		//                            thread 1 (ZOMBIE)
		// ptable <-----------------> thread 2 (SLEEPING)
		// If this happen in sched2, ptable's proc is thread 2
		// But context is still thread1
		// then sched() will be called, come to scheduler.
		// swtch(&(c->scheduler), c->proc->context) is called
		// But ptable's context is thread 1's context
		// Go back to the ZOMBIE's context => error
		if (c->proc->state == ZOMBIE) {
			pp = ppTable + (c->proc - ptable.proc);

			// Swap ptable's proc to the SLEEPING lwp
			swap(pp, SLEEPING);
		}
		
		c->proc = 0;
	}

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1){
    cprintf("ncli: %d\n", mycpu()->ncli);
    panic("sched locks");
  }
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  
  struct proc* p = myproc();
  struct procParse* pp = ppTable + (p - ptable.proc);

  p->state = RUNNABLE;

  // Before go to the scheduler,
  // Check who scheduled this process
  // If this process is scheduled by stride queue,
  // Push this process again to the stride queue with changed pass
  if (pp->level == -1) {
	  PushStrideQueue(&strideQ, pp);
  }

  sched();

  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
	release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  // Do not go to the scheduler immediately
  // At first, go to the same lwp group
  sched2();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
	struct proc *p;
	struct procParse* pp;

	// Wake up all threads SLEEPING on this chan
	int pIndex = 0;
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    	pp = ppTable + pIndex;

		for (int pn = 0; pn < NTHREADPAGE; pn++) {
			if (pp->threadDir[pn] == 0) {
				continue;
			}

			struct Thread* t = 0;
			for (int i = 0; i < NTHREAD; i++) {
				t = &(pp->threadDir[pn]->threadArr[i]);
				
				if (t->p == 0) {
					continue;
				}
				else if (t->p->state == SLEEPING && t->p->chan == chan) {
					t->p->state = RUNNABLE;
				}

			}
		}
		pIndex++;
  	}
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
getppid(void)
{
	return myproc()->parent->pid;
}

int
getlev(void)
{
	/* Get the process's level.
	 * If process is managed by stride scheduler,
	 * process's level is -1 */
	int pIndex = myproc() - ptable.proc;
	struct procParse* pp = ppTable + pIndex;

	return pp->level;
}

int
set_cpu_share(void)
{
	int ret = 0;
	int pIndex = myproc() - ptable.proc;
	struct procParse* pp = ppTable + pIndex;

	int ticket;
	argint(0, &ticket); // Bring the first argument.

	/* If required ticket is 0 */
	if (ticket == 0) {
		/* If this process is scheduled by stride scheduler,
		 * move it to the mlfq and give ticket back to the g_ticket.
		 * If this process is scheduled by mlfq, do nothing. */
		if (pp->level == -1) {
			acquire(&ticketbox.lock);
			ticketbox.ticket += pp->ticket;
			release(&ticketbox.lock);

			pp->ticket = 0;

			/* InsertMLFQ make pp's level to the 0.
			 * So this process will be removed at SearchStrideQueue.
			 * Because this process's level is not -1. */
			acquire(&ptable.lock);
			InsertMLFQ(&mlfq, pp);
			release(&ptable.lock);
		}

		return 0;
	}

	// Before insert process to the stride scheduler,
	// Must determine the process's fisrt pass.
	// This will be determined by comparing
	// MLFQ's pass and stride scheduler's minimum pass.

	/* Get minimum pass of stride scheduler.
	 * But if process is dead, remove it and select again. */
	acquire(&ptable.lock);
	double minPass = -1;
	int originalSize = strideQ.size;
	struct procParse* top = 0;
	for (int i = 0; i < originalSize; i++) {
		top = GetTopStrideQueue(&strideQ);

		/* If process is dead, remove it. */
		if (top->p->state == UNUSED || top->p->state == ZOMBIE) {
			/* Give ticket back to global ticket */
			acquire(&ticketbox.lock);
			ticketbox.ticket += top->ticket;
			release(&ticketbox.lock);

			/* Initialize top */
			top->pass = 0;
			top->stride = 0;
			top->ticket = 0;
			top->usedTick = 0;
			top->usedQuantumTick = 0;
			top->level = -1;

			PopStrideQueue(&strideQ);
		}
		else {
			/* Regardless of whether process is runnable or not,
			 * Just get the minimum pass */
			minPass = top->pass;
			break;
		}
	}

	/* If stride scheduler has no process */
	if (minPass == -1) {
		minPass = mlfq.pass; // Set the min pass to the MLFQ's pass
	}
	/* If stride scheduler has process */
	else {
		minPass = minPass <= mlfq.pass ? minPass : mlfq.pass;
		// Choose lower pass
	}

	ret = InsertStrideQueue(&strideQ, pp, ticket, minPass);
	release(&ptable.lock);

	return ret;
}

struct procParse*
schedule(void)
{
	struct procParse* pp = 0;
	
	/* If both scheduler have no process */
	if (mlfq.size == 0 && strideQ.size == 0) {
		// Do nothing
	}
	else if (strideQ.size == 0) {
		/* If stride scheduler has no process,
		 * Select the process from MLFQ. */

		pp = SearchMLFQ(&mlfq);
		
		// It is possible that MLFQ has process but no runnable process.
		// In this case, do nothing.
	}
	else if (mlfq.size == 0) {
		/* IF MLFQ has no process,
		 * Select the process from stride scheduler. */

		pp = SearchStrideQueue(&strideQ);

		/* If stride scheduler has runnable process */
		if (pp != 0) {
			pp->usedQuantumTick = 0;
		}

		// It is possible that
		// stride scheduler has process but no runnable process.
		// In this case, do nothing.
	}
	else {
		/* If both scheduler have proceses,
		 * Compare their pass and choose scheduler who has lower pass. */

		/* Get the process who has lowest pass and runnable
		 * in the stride scheduler */
		pp = SearchStrideQueue(&strideQ);

		if (pp == 0) {
			/* If stride scheduler has process
			 * But there are no runnable process,
			 * Change to MLFQ */
			pp = SearchMLFQ(&mlfq);
		}
		else if (pp->pass <= mlfq.pass) {
			/* Stride scheduler has runnable process and has lower pass.*/
			pp->usedQuantumTick = 0;
		}
		else {
			/* MLFQ has lower pass, change process to MLFQ's process.
			 * But before changing, save stride scheduler's process.
			 * Because if MLFQ has no runnable process,
			 * It will be changed again to stride scheduelr's process. */
			struct procParse* tmp = pp;

			/* Change process to MLFQ's process */
			pp = SearchMLFQ(&mlfq);

			if (pp != 0) {
				/* If mlfq has runnable process,
				 * Stride Scheduler's process does not need anymore.
				 * Push it again */
				PushStrideQueue(&strideQ, tmp);
			}
			else {
				/* If MLFQ has no runnable process,
				 * change again to stride scheduler's process.
				 * It happens when MLFQ has lower pass and has processes,
				 * But there are no runnable process. */
				pp = tmp;
				pp->usedQuantumTick = 0;
			}
		}
	}

	return pp;
}

int 			
thread_create(void) {
	struct proc* curproc = myproc();
	struct procParse* pp = ppTable + (curproc - ptable.proc);
	struct ThreadId retId;

	/* Get user mode's arguments */
	struct ThreadId* argId = 0;
	void* (*start_routine)(void*) = 0;
	void* arg = 0;

	if (argint(0, (int*)&argId) == -1) {
		panic("thread_create argument1 error\n");
	}
	if (argint(1, (int*)&start_routine) == -1) {
		panic("thread_create argument2 error\n");
	}
	if (argint(2, (int*)&arg) == -1) {
		panic("thread_create argument3 error\n");
	}

	/* Fork new thread */
	retId = ForkThread(pp, start_routine, arg);

	/* Give user thread_t */
	argId->pageNum = retId.pageNum;
	argId->tid = retId.tid;
	
	if (retId.tid == -1) {
		cprintf("thread_create err: ret id is no id\n");
		return -1;
	}

	return 0;
}

void
thread_exit() {
  struct proc *curproc = myproc();
  struct procParse* pp = ppTable + (curproc - ptable.proc);
  struct Thread* curthread = pp->threadNow;
 
  acquire(&ptable.lock);

  // Get argument of thread_exit system call
  void* arg = 0;
  if (argint(0, (int*)&arg) < 0) {
  	panic("thread_exit argument 0 err\n");
  }

  // Give argument data to thread
  curthread->retval = arg;

  // Threads joining this thread might be SLEEPING.
  struct Thread* t = curthread->head;
  while (t != 0) {
  	t->p->state = RUNNABLE; // Make it RUNNABLE
	t = t->next;
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; // curproc is ptable's proc
  sched2();
  panic("zombie thread exit");
}

int
thread_join() {
	struct procParse* pp = ppTable + (myproc() - ptable.proc);
	struct Thread* curthread = pp->threadNow;
	struct Thread* target = 0;

	acquire(&ptable.lock);
	
	// Get argument of thread_join system call
	struct ThreadId argId;
	void** retval;
	
	if (argint(0, (int*)&argId) < 0 ) {
		panic("thread_join argument 0 err\n");
	}
	if (argint(1, (int*)&retval) < 0) {
		panic("thread_join argument 1 err\n");
	}

	// User want to join argId's thread
	int pn = argId.pageNum;
	unsigned int i = argId.tid % NTHREAD;
	target = &(pp->threadDir[pn]->threadArr[i]); // join target

	// If there are no target thread, join is failed
	if (target->tid != argId.tid) {
		release(&ptable.lock);
		cprintf("thread_join err: no target thread\n");
		return -1;
	}
	else {
		/* Connect target with current thread */
		if (target->tail == 0) {
			target->head = curthread;
			target->tail = curthread;
			curthread->next = 0;
		}
		else {
			struct Thread* t = target->tail;
			t->next = curthread;
			target->tail = curthread;
			curthread->next = 0;
		}

	  	// If target is not ZOMBIE, go to SLEEP
		// else just get target's return value and go back to user code
		if (target->p->state != ZOMBIE) {
  			curthread->p->state = SLEEPING;
  			sched2();
		}
	}

	// If retval is required, give it.
	if (retval != 0) {
		*retval = target->retval; // *(void**) = void*
	}

	// If this thread is tail of target
	// Remove target
	if (curthread == target->tail) {
		if (target->p->state != ZOMBIE) {
			panic("thread_join: why target is not ZOMBIE?\n");
		}

		if (FreeThread(pp, target) == -1) {
			release(&ptable.lock);
			cprintf("thread_join err: FreeThread failed\n");
			return -1;
		}
	}
	
	release(&ptable.lock);

	return 0;
}

void
addticks(uint lastTick) {
	struct procParse* pp = ppTable + (myproc() - ptable.proc);

	/* If this tick is already updated */
	if (pp->lastTick >= lastTick) {
		return;
	}

	// If process managed by stride scheduler
	if (pp->level == -1) {
		pp->pass += pp->stride;
		pp->usedQuantumTick += 1;
	}
	// If process managed by mlfq
	else {
		pp->usedTick += 1;
		pp->usedQuantumTick += 1;
		mlfq.usedTick += 1;
		mlfq.pass += mlfq.stride;
	}
	pp->lastTick = lastTick; // To prevent overlapping addition
}

int
checkquantum() {
	struct procParse* pp = ppTable + (myproc() - ptable.proc);

	// If process used all time quantum
	if ((pp->level == -1 && pp->usedQuantumTick >= TIME_QUANTUM_STRIDE)
		|| (pp->level == 0 && pp->usedQuantumTick >= TIME_QUANTUM_LEVEL0)
		|| (pp->level == 1 && pp->usedQuantumTick >= TIME_QUANTUM_LEVEL1)
		|| (pp->level == 2 && pp->usedQuantumTick >= TIME_QUANTUM_LEVEL2)
			) {
		
		return 1;
	}
	// Process has time quantum yet
	else {
		return 0;
	}
}

// Get thread id
int 
gettid() {
	struct procParse* pp = ppTable + (myproc() - ptable.proc);
	return pp->threadNow->tid;
}

// Round Robin LWP Scheduling
void
sched2() 
{
	struct proc* p = myproc(); // ptable' proc
	struct procParse* pp = ppTable + (p - ptable.proc);
	int intena;
  
  	if(!holding(&ptable.lock))
    	panic("sched ptable.lock");
  	if(mycpu()->ncli != 1){
    	cprintf("ncli: %d\n", mycpu()->ncli);
    	panic("sched locks");
  	}
  	if(p->state == RUNNING)
    	panic("sched running");
  	if(readeflags()&FL_IF)
    	panic("sched interruptible");

	// Previous ptable's proc
	struct proc* prev = swap(pp, RUNNABLE);

	// If there are no RUNNABLE
	if (prev == 0) {
		// Before go to the scheduler,
  		// Check who scheduled this process
  		// If this process is scheduled by stride queue,
  		// Push this process again to the stride queue with changed pass
  		if (pp->level == -1) {
      		PushStrideQueue(&strideQ, pp);
  		}

		// Go to scheduler
		sched();
	}
	// Else if ptable's proc is changed to new RUNNABLE
	else if (p->context != prev->context) {
		// Reset kernel stack's information
  		mycpu()->ts.ss0 = SEG_KDATA << 3;
  		mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
 
		p->state = RUNNING; // Swapped ptable's proc

		intena = mycpu()->intena;
		swtch(&(prev->context), p->context);
		mycpu()->intena = intena;
	}
	// no swapped, but stil RUNNABLE
	else {
		if (p->state != RUNNABLE) {
			panic("sched2 err: swap return no 0, But no RUNNABLE\n");
		}

		p->state = RUNNING; // ptable's proc is not changed
	}
}

void
yield2() 
{
  acquire(&ptable.lock);
  myproc()->state = RUNNABLE;
  sched2();
  release(&ptable.lock);
}
