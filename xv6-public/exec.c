#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "thread.h"
#include "spinlock.h"
#include "ticketbox.h"
#include "mlfq.h"

extern struct TicketBox ticketbox;

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

extern struct procParse ppTable[NPROC];

extern struct MLFQ mlfq;



// Wake up threads who are sleeping on chan
void
wakeupthread(struct procParse* pp, void* chan)
{
	struct ThreadPage* pg = 0;
	struct Thread* t = 0;

	for (int pn = 0; pn < NTHREADPAGE; pn++) {
		pg = pp->threadDir[pn];

		if (pg == 0) {
			continue;
		}

		for (int i = 0; i < NTHREAD; i++) {
			t = &(pg->threadArr[i]);

			if (t->p != 0 && t->p->state == SLEEPING &&
					t->p->chan == chan) {
				t->p->state = RUNNABLE;
				t->p->chan = 0;
			}
		}
	}
}


int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();
  struct procParse* pp = ppTable + (curproc - ptable.proc);

  // Check exec flag
  // If exec flag is 1, it means that another thread is doing exec
  // Then, go to sleep
  acquire(&ptable.lock);
  while (pp->execFlag) {
      sleep(&pp->execFlag, &ptable.lock);
  }
  pp->execFlag = 1;
  release(&ptable.lock);

  // Start exec

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
	
	// exec failed, wake up another threads who are waiting for exec
	acquire(&ptable.lock);
	pp->execFlag = 0;
	wakeupthread(pp, &pp->execFlag);
	release(&ptable.lock);

    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  // + initialize thread info
  acquire(&ptable.lock);

  // Pop every Address stack
  int size = pp->trashAddrStack.size;
  for(int i = 0; i < size; i++) {
      PopAddrStack(&pp->trashAddrStack);
  }

  // Remove thread directory's pages
  struct ThreadPage* pg = 0;
  struct Thread* t = 0;
  for(int pn = 0; pn < NTHREADPAGE; pn++) {
      pg = pp->threadDir[pn];

      if (pg == 0) {
	      continue;
	  }

	  // Check threads
	  // If threads has kernel stack, free it
	  for (int i = 0; i < NTHREAD; i++) {
	      t = &(pg->threadArr[i]);

		  // Do not touch ptable's proc
		  if (t->p == &(t->lwp) && t->p->kstack != 0) {
		      kfree((char*)t->p->kstack);
		  }
	  }
				
	  // Free thread page
	  // Except page 0 for new main thread
	  if (pn > 0) {
	      kfree((char*)pg);
	      pp->threadDir[pn] = 0;
	  }
  }

  // Remove old page directory and old process's user memory,
  // Set new process information
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);

  // Allocate new main thread
  pg = pp->threadDir[0];
  t = &(pg->threadArr[0]);
  memset(pg, 0, sizeof(struct ThreadPage));

  t->pageNum = 0;
  t->tid = 0;
  pg->threadNum = 1; // main thread
  pp->tid = 1; // next thread id

  // Copy proc
  memmove(&(t->lwp), curproc, sizeof(struct proc));
  
  // this thread points ptable
  t->p = curproc;
  pp->threadNow = t;

  // change exec flag
  // But there are no waiting thread. So no need to call wakeupthread.
  pp->execFlag = 0;

  /* Process is changed, Initailize scheduling info */
  // If previous process is scheduled by stride scheduler
  if (pp->level == -1) {
	  // Give ticket back to the ticketbox
	  // Then insert into mlfq
      acquire(&ticketbox.lock);
	  ticketbox.ticket += pp->ticket;
	  release(&ticketbox.lock);

	  pp->ticket = 0;

	  InsertMLFQ(&mlfq, pp);
  }
  else if (pp->level == 0) {
  	  // Prevous process must be placed front of levelqueue
	  if (GetFrontLevelQueue(&mlfq.qLevel0) != pp) {
	      panic("exec err: why current process is not front of queue?\n");
	  }
	  
	  // Pop previous process from level queue
	  PopLevelQueue(&mlfq.qLevel0);
	  mlfq.size--;

	  // Push new process into level queue
  	  InsertMLFQ(&mlfq, pp);
  }
  else if (pp->level == 1) {
  	  // Prevous process must be placed front of levelqueue
	  if (GetFrontLevelQueue(&mlfq.qLevel1) != pp) {
	      panic("exec err: why current process is not front of queue?\n");
	  }

	  // Pop previous process from level queue
	  PopLevelQueue(&mlfq.qLevel1);
	  mlfq.size--;

	  // Push new process into level queue
  	  InsertMLFQ(&mlfq, pp);
  }
  else {
      // Prevous process must be placed front of levelqueue
	  if (GetFrontLevelQueue(&mlfq.qLevel2) != pp) {
	      panic("exec err: why current process is not front of queue?\n");
	  }

	  // Pop previous process from level queue
	  PopLevelQueue(&mlfq.qLevel2);
	  mlfq.size--;

	  // Push new process into level queue
  	  InsertMLFQ(&mlfq, pp);
  }
  
  release(&ptable.lock);
  
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }

  // exec failed, wake up another threads who are waiting for exec
  acquire(&ptable.lock);
  pp->execFlag = 0;
  wakeupthread(pp, &pp->execFlag);
  release(&ptable.lock);  

  return -1;
}
