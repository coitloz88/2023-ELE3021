#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// find process according to the given pid (when there is no pid matched, return null(0))
struct proc *
findProcessByPid(int pid)
{
  struct proc *p;
  struct proc *targetProc = 0;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid && p->state != UNUSED)
    {
      targetProc = p;
      break;
    }
  }
  release(&ptable.lock);

  return targetProc;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->stackpages = 1;
  p->mlimit = 0;
  p->tid = 0;
  p->mthread = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
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
  p->tf->eip = 0; // beginning of initcode.S

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
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc *mthread = curproc->mthread;

  if (!(mthread) || curproc->tid == 0) // mthread가 없으면, 본인이 mthread라는 뜻이므로
    mthread = curproc;                 // mthread를 current proc으로 설정해준다.

  sz = mthread->sz;

  if (n > 0)
  {
    // t2: exceed memory limitation
    if (mthread->mlimit != 0 && sz + n > mthread->mlimit)
    {
      // TODO: sub thread도 memory limit를 개별적으로 가지나?
      // 애초에 sub thread는 이전의 합이 아닌 본인만의 memory를 가지는게 맞... 가 아니라 stack bottom을 저장하는 거니까 커지는게 맞구나
      // 그러면 mthread의 sz가 커지면 안되는거 아닌가? 흠 mthread만의 stack 크기만을 가져야하는데...
      return -1;
    }

    if ((sz = allocuvm(mthread->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(mthread->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  mthread->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  *np->tf = *curproc->tf;
  np->mlimit = curproc->mlimit;

  if (np->tid == 0)
  {
    np->parent = curproc;
  }
  else
  {
    np->parent = curproc->mthread;
  }

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // exit all threads
  acquire(&ptable.lock);

  // clean other threads
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == curproc->pid && p->tid != curproc->tid) // pid는 같은데 process가 같지는 않은 경우
    {
      cleanThread(p);
    }
  }

  release(&ptable.lock);

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
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
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
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
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
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
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
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
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

/**
 * t2: memory limit function
 * pid: memory limit을 지정할 프로세스 pid
 * limit: 프로세스가 사용할 수 있는 memory limit(byte 단위)
 */
int setmemorylimit(int pid, int limit)
{
  if (pid < 2)
  {
    cprintf("[err] cannot set memory limitation for kernel process\n");
    return -1;
  }

  // memory limit의 값은 0 이상의 정수
  if (limit < 0)
    return -1;

  // 해당되는 pid를 찾아서 메모리 할당 limit 설정
  struct proc *p = findProcessByPid(pid);

  if (p == 0) // invalid pid, 해당되는 프로세스가 없음
  {
    return -1;
  }

  if (p->pid == pid)
  {
    if (limit < p->sz) // 기존 할당받은 메모리보다 limit가 작은 경우 -1 반환
    {
      return -1;
    }
    else
    {
      acquire(&ptable.lock);
      p->mlimit = limit;
      release(&ptable.lock);
    }
  }

  return 0; // 정상 동작, 0 반환
}

/**
 * t3: pmanager show process list
 */
void showProcessList(void)
{
  cprintf("---------------------------------------------------------------\n");

  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == RUNNABLE || p->state == RUNNING || p->state == SLEEPING)
    {
      if (p->tid == 0 || p->mthread == 0)
      {
        cprintf("name: %s | ", p->name);
        cprintf("pid: %d | ", p->pid);
        cprintf("stack pages: %d | ", p->stackpages);
        cprintf("memory: %d | ", p->sz);
        cprintf("memlim: %d | ", p->mlimit);
        cprintf("\n");
        struct proc *q;

        // thread를 고려하기 위해, ptable을 한번 더 돌면서 mthread를 찾아줌
        for (q = ptable.proc; q < &ptable.proc[NPROC]; q++)
        {
          if (q->pid == p->pid && q != p && q->mthread == p)
          {
            cprintf("   thread for process %d with thread id: %d\n", p->pid, q->tid);
          }
        }
      }
    }
  }

  cprintf("---------------------------------------------------------------\n");
}

void cleanThread(struct proc *p)
{
  p->state = UNUSED;
  kfree(p->kstack);
  p->kstack = 0;
  p->pid = 0;
  p->parent = 0;
  p->killed = 0;
  p->name[0] = 0;
  p->tid = 0;
  p->mthread = 0;
}

void cleanOtherThreadsForExec(int pid, int tid)
{
  struct proc *p;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid && p->tid != tid)
    {
      release(&ptable.lock);
      // Close all open files.
      int fd;
      for (fd = 0; fd < NOFILE; fd++)
      {
        if (p->ofile[fd])
        {
          fileclose(p->ofile[fd]);
          p->ofile[fd] = 0;
        }
      }

      begin_op();
      iput(p->cwd);
      end_op();
      p->cwd = 0;

      acquire(&ptable.lock);
      cleanThread(p);
    }
  }

  release(&ptable.lock);
}

void makeMainThread(struct proc *p)
{
  acquire(&ptable.lock);
  p->mthread = 0;
  p->tid = 0;
  release(&ptable.lock);
}

// t4: threading
/**
 * create new thread: fork & exec
 * thread: 스레드의 id 지정
 * start_routine: 스레드가 시작할 함수를 지정
 *    스레드는 start_routine이 가리키는 함수에서 시작하게 됨
 * arg: 스레드의 start_routine에 전달할 인자
 * return: 스레드가 성공적으로 만들어진 경우: 0, 에러가 있으면 -1
 */
int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
  /* fork part */
  // allocproc을 통해 새로운 프로세스 공간을 할당(프로세스를 생성하는 것처럼 스레드를 생성)
  // 각종 멤버 변수 초기화를 해주는 part
  // 단, 프로세스 fork와 다르게 기존 pgdir을 복사하지 않고, main thread의 pgdir을 그대로 사용, 공유 리소스(data, text 등)를 공유
  // register 정보, stack은 따로 둔다.

  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  // 새로운 LWP를 ptable에 생성한다.
  // - ptable의 UNUSED 상태의 entry를 찾아서 멤버 변수를 초기화해준다.
  // - 생성될 thread만의 kernel stack을 할당하고, trap frame과 context를 셋업해준다.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }
  --nextpid; // allocproc에서 nextpid가 증가하지만, thread를 만들 때는 pid가 증가하면 안됨

  struct proc *mthread; // main thread를 가리키는 포인터
  if (curproc->mthread) // 스레드를 새로 만드려는 lwp에 이미 main thread가 존재하면
  {
    mthread = curproc->mthread; // 해당 mthread 포인터를 찾아준다.
  }
  else
  {
    mthread = curproc; // 없으면 현재 스레드를 생성하려는 프로세스를 메인 스레드로 설정해준다.
  }

  // Copy process state from main thread
  np->parent = mthread->parent;
  np->pid = mthread->pid; // main process와 동일한 pid
  np->tid = nexttid++;    // thread 간의 구별을 위한 tid 할당(커널에서 할당하는 것임)
  *np->tf = *mthread->tf;
  np->mthread = mthread;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  // copy opened file
  for (int i = 0; i < NOFILE; i++)
    if (mthread->ofile[i])
      np->ofile[i] = filedup(mthread->ofile[i]);

  // copy current directory
  np->cwd = idup(mthread->cwd);

  // copy the name of process
  safestrcpy(np->name, mthread->name, sizeof(mthread->name));

  /* exec part */
  pde_t *pgdir = mthread->pgdir; // 원래의 pgdir을 새로 복사한 페이지가 아닌, mthread가 사용하는 pgdir을 그대로 사용(메모리 공간 공유)
  uint sz = mthread->sz;
  uint sp = sz;

  uint arguments[2];

  // TODO: user stack에 남는 메모리 공간이 있는지 확인하는 로직 추가

  // user stack을 새롭게 할당해줌(stack은 공유하지 않으므로)
  // 2개의 PGSIZE 할당: 하나는 user stack, 하나는 guard page
  if ((sz = allocuvm(pgdir, sz, sz + 2 * PGSIZE)) == 0)
  {
    goto bad;
  }
  clearpteu(pgdir, (char *)(sz - 2 * PGSIZE)); // guard page에 접근할 수 없도록 설정

  // TODO: mthread에 걸린 memory limit은 해당 mthread가 가지는 모든 thread의 sz 합? 혹은 각 스레드마다 개별 mlimit을 가지나?
  // pid를 기준으로 mlimit을 정해주기 때문에... 하나의 프로세스가 가지는 mlimit이 정해져있고 이는 해당 프로세스의 모든 스레드의 sz 총합일 것 같음
  // 우선은 그렇게 설정
  // 그렇게 하자

  // arguments setting: 새로운 thread를 실행하기 위한 user stack 설정
  arguments[0] = 0xFFFFFFFF; // return address
  arguments[1] = (uint)arg;  // arguments
  sp -= 8;                   // 2 * 4

  if (copyout(pgdir, sp, arguments, 8) < 0)
  {
    deallocuvm(pgdir, sz, sz + 2 * PGSIZE); // copy에 실패하면 deallocate하고 bad로 가서 -1을 return
    goto bad;
  }

  // sz가 memory limit을 초과했는지 확인
  if (mthread->mlimit != 0 && sz > mthread->mlimit)
  {
    deallocuvm(pgdir, sz, sz + 2 * PGSIZE); // copy에 실패하면 deallocate하고 bad로 가서 -1을 return
    goto bad;
  }

  mthread->sz = sz; // mthread에서 할당된 stack 최상위 값을 가리키도록 함(이후 stack 할당도 차곡차곡...)

  // commit to user image
  np->sz = sz;
  np->pgdir = pgdir;

  np->tf->eip = (uint)start_routine; // 실행할 명령 주소
  np->tf->esp = sp;                  // stack의 가장 아래 부분

  /* thread create 완료 */

  *thread = np->tid;

  acquire(&ptable.lock);

  np->state = RUNNABLE; // scheduler에 의해 스케줄링 될 수 있도록 함

  release(&ptable.lock);

  return 0;

bad:
  np->state = UNUSED;
  return -1;
}

/**
 * exit current thread: exit
 * 스레드를 종료하고 값(retval)을 반환
 * mthread에서 종료될 thread 자원을 정리
 * retval: 스레드 종료 후, join 함수에서 받아갈 값
 */
void thread_exit(void *retval)
{
  // 모든 스레드는 이 함수를 통해 종료(시작 함수 끝에 도달하는 경우 고려X)
  struct proc *curproc = myproc();
  int fd;

  curproc->retval = retval;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent or main thread might be sleeping in wait().
  if (curproc->mthread)
  {
    wakeup1(curproc->mthread);
  }
  else
  {
    wakeup1(curproc->parent);
  }

  // children 프로세스 정리해서 ZOMBIE로 만들 필요가 없으므로 생략..

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

/**
 * wait for the termination of the thread and get return value: wait
 * thread: join할 스레드 id
 * retval: 스레드가 반환한 값 저장
 * return: 정상적인 join이면 0, 아니면 -1
 */
int thread_join(thread_t thread, void **retval)
{
  struct proc *p;
  int haveThread; // 자식이 있는지 찾는게 아니라, 종료된 스레드가 있는지 찾아야함
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited threads.
    haveThread = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->tid != thread)
        continue;
      haveThread = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        *retval = p->retval;
        cleanThread(p);

        release(&ptable.lock);
        return 0;
      }
    }

    // No point waiting if we don't have any threads.
    if (!haveThread || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
