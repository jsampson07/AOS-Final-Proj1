#ifndef __GT_UTHREAD_H
#define __GT_UTHREAD_H

/* User-level thread implementation (using alternate signal stacks) */

typedef unsigned int uthread_t;
typedef unsigned int uthread_group_t;

/* uthread states */
#define UTHREAD_INIT 0x01
#define UTHREAD_RUNNABLE 0x02
#define UTHREAD_RUNNING 0x04
#define UTHREAD_CANCELLED 0x08
#define UTHREAD_DONE 0x10

/* uthread struct : has all the uthread context info */
typedef struct uthread_struct
{

	int uthread_state; /* UTHREAD_INIT, UTHREAD_RUNNABLE, UTHREAD_RUNNING, UTHREAD_CANCELLED, UTHREAD_DONE */
	int uthread_priority; /* uthread running priority */

	long uthread_credits; /* uthread credit amounts (in microseconds)*/
	int uthread_exact_credits; /* used for printing purposes (as the literal credit amount) */
	long uthread_init_credits; /* initial uthread credit amount (IN MICROSECONDS) ==> USED FOR REFRESHING*/
	int is_over; /* whether or not uthread ran out of credits (0 - no, 1 - yes) */
	struct timeval start_time; /* when the thread started executing */

	struct timeval time_created; /* time when uthread was first created (not scheduled) */
	struct timeval time_completed; /* time whne uthread finished execution completely */
	long total_cpu_time; /* time spent by a uthread running on a CPU every time it is scheduled */
	long exec_time; /* a thread's execution time (time_completed - time_created) */

	int cpu_id; /* cpu it is currently executing on */
	int last_cpu_id; /* last cpu it was executing on */

	uthread_t uthread_tid; /* thread id */
	uthread_group_t uthread_gid; /* thread group id  */
	int (*uthread_func)(void*);
	void *uthread_arg;

	void *exit_status; /* exit status */
	int reserved1;
	int reserved2;
	int reserved3;

	sigjmp_buf uthread_env; /* 156 bytes : save user-level thread context*/
	stack_t uthread_stack; /* 12 bytes : user-level thread stack */
	TAILQ_ENTRY(uthread_struct) uthread_runq;
} uthread_struct_t;

struct __kthread_runqueue;
extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(struct __kthread_runqueue *));
extern int is_credit_scheduler;
extern void gt_yield(uthread_struct_t *running_t, int enabled);
extern int enable_yield;
extern int enable_lb;
extern uthread_struct_t *all_uthreads[];
#endif
