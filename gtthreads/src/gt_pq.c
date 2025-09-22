#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>

#include "gt_include.h"


/**********************************************************************/
/* runqueue operations */
static void __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);
static void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);


/**********************************************************************/
/* runqueue operations */
static inline void __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_INSERT_TAIL(uhead, u_elem, uthread_runq);

	/* Update information */
	if(!IS_BIT_SET(runq->prio_array[uprio].group_mask, ugroup))
		SET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot++;

	runq->uthread_prio_tot[uprio]++;
	if(!IS_BIT_SET(runq->uthread_mask, uprio))
		SET_BIT(runq->uthread_mask, uprio);

	runq->uthread_group_tot[ugroup]++;
	if(!IS_BIT_SET(runq->uthread_group_mask[ugroup], uprio))
		SET_BIT(runq->uthread_group_mask[ugroup], uprio);

	return;
}

static inline void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_REMOVE(uhead, u_elem, uthread_runq);

	/* Update information */
	if(TAILQ_EMPTY(uhead))
		RESET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot--;

	if(!(--(runq->uthread_prio_tot[uprio])))
		RESET_BIT(runq->uthread_mask, uprio);

	if(!(--(runq->uthread_group_tot[ugroup])))
	{
		assert(TAILQ_EMPTY(uhead));
		RESET_BIT(runq->uthread_group_mask[ugroup], uprio);
	}

	return;
}



int kthread_lb_priority(kthread_runqueue_t *empty_kt, kthread_runqueue_t *best_kt) {
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *to_switch = NULL;
	unsigned int uprio, ugroup;

	gt_spin_lock(&best_kt->kthread_runqlock);
	runqueue_t *best_rq = best_kt->active_runq;

	uprio = LOWEST_BIT_SET(best_rq->uthread_mask);

	prioq = &(best_rq->prio_array[uprio]);

	assert(prioq->group_mask);
	ugroup = LOWEST_BIT_SET(prioq->group_mask);

	u_head = &(prioq->group[ugroup]);
	to_switch = TAILQ_FIRST(u_head);

	//Remove the uthread from best_rq and add to empty_rq (ACTIVE RUNQs) !!!!

	/* When I remove this thread I need to remove it from the active rq from best_rq and then 
	add it to the active rq of empty_rq */

	if (!to_switch) {
		gt_spin_unlock(&best_kt->kthread_runqlock);
		return 0;
	}

	gt_spin_unlock(&best_kt->kthread_runqlock);
	switch_runqueue(best_rq, &(best_kt->kthread_runqlock), empty_kt->active_runq, &(empty_kt->kthread_runqlock), to_switch);

	return 1;
}

int kthread_lb_credit(kthread_runqueue_t *empty_kt, kthread_runqueue_t *best_kt) {
    //IMPLEMENT THIS BASED ON CREDIT SCHEDULING
    prio_struct_t *prioq;
    uthread_head_t *u_head;
    uthread_struct_t *to_switch = NULL;
    unsigned int uprio, ugroup;
    runqueue_t *empty_rq, *best_rq = NULL;
    long most_creds = 0;


    //we need to keep track of best_rq when looping
    //we need to keep track of the "best thread with credit"
    if (!best_kt) {
        return 0;
    }
    gt_spin_lock(&best_kt->kthread_runqlock);
    best_rq = best_kt->active_runq;
    if (!best_rq) {
        gt_spin_unlock(&best_kt->kthread_runqlock);
        return 0;
    }
    unsigned int uthread_mask = best_rq->uthread_mask;


    if (!uthread_mask) {
        //gt_spin_unlock(&best_kt->kthread_runqlock);
        gt_spin_unlock(&best_kt->kthread_runqlock);
        return 0;
    }


    //int exist_pos = 0; // Added: for testing !!!!!
    while (uthread_mask) {
        uprio = LOWEST_BIT_SET(uthread_mask);
        uthread_mask &= ~(1 << uprio);
        prioq = &(best_rq->prio_array[uprio]);


        //assert(prioq->group_mask); LETS LEAVE IT WITHOUT FIRST AND DO A DIRECT CHECK
        if (!prioq->group_mask) {
            continue;
        }
        unsigned int group_mask = prioq->group_mask;


        while(group_mask) {
            ugroup = LOWEST_BIT_SET(group_mask);
            group_mask &= ~(1 << ugroup);
            uthread_struct_t *u_curr = TAILQ_FIRST(&(prioq->group[ugroup]));
            while (u_curr) {
                //long elapsed_time = ((curr_time.tv_sec - u_curr->start_time.tv_sec) * 1000L) + ((curr_time.tv_usec - u_curr->start_time.tv_usec) / 1000L);
                /*
                printf(
                    "uthread_id (%d), uthread_gid (%d), uthread_credits (%ld)\n",
                    u_curr->uthread_tid,
                    u_curr->uthread_gid,
                    u_curr->uthread_credits
                );
                */
                if ((u_curr->uthread_credits > most_creds) && !(u_curr->is_over) && (u_curr->uthread_state == UTHREAD_RUNNABLE)) { // if this thread has new most creds and is NOT over (<=0)
                    //exist_pos = 1; // Added: for testing !!!!
                    to_switch = u_curr;
                    most_creds = u_curr->uthread_credits;
                }
                u_curr = TAILQ_NEXT(u_curr, uthread_runq);
            }
        }
    }


    //gt_spin_unlock(&best_kt->kthread_runqlock);


    if (to_switch) {
        // does not acquire lock
        __rem_from_runqueue(best_rq, to_switch);
        gt_spin_unlock(&best_kt->kthread_runqlock);
        add_to_runqueue(empty_kt->active_runq, &empty_kt->kthread_runqlock, to_switch);
        return 1;
    }
    gt_spin_unlock(&best_kt->kthread_runqlock);
    return 0;
}

/**
 * When a kthread's runqueue is empty, we want to "steal" from another kthread with credits in their active runq
 * GOAL: we want to find the highest credit uthread in other kthreads?????
 * 
 * NOTE: if we call this function, we GUARANTEE that our runqueue is empty given where we call it inside of uthread_schedule()
 */
int kthread_load_balance(kthread_runqueue_t *curr_kt) {
	if (!enable_lb || !curr_kt) {
		return 0;
	}
	kthread_runqueue_t *best_kt = NULL;
	int most_threads = 0;
	/* typical load balancing apparently involves assigning "weights" to each entity, in this case,
	entity = kthread, based on number of uthreads it has and taking uthreads from kthread with most
	amt, and giving to empty kthread */
	//here: no weights, so just manually access uthread_tot for each kthread and find one with greatest
	//steal from that kthread and give to "curr_kt"
	for (int i = 0; i < GT_MAX_KTHREADS; i++) {
		kthread_context_t *this_kthread = kthread_cpu_map[i];
		if (this_kthread == NULL) { // skip this thread, nothing to look at !
			continue;
		}
		kthread_runqueue_t *this_kthread_rq = &(this_kthread->krunqueue);
		if (this_kthread_rq == curr_kt) {
			continue;
		}
		//Now we can check how many total threads it has in its runqueue
		int this_total_threads = this_kthread_rq->active_runq->uthread_tot;
		if (this_total_threads > most_threads) {
			most_threads = this_total_threads;
			best_kt = this_kthread_rq;
		}
	}
	if (!best_kt) {
		return 0;
	}
	if (is_credit_scheduler == 0) {
		return kthread_lb_priority(curr_kt, best_kt);
	} else {
		return kthread_lb_credit(curr_kt, best_kt);
		/*
		if (returned) {
			printf("******************************************************\n");
			printf("LOAD BALANCE: success !!!\n");
			printf("******************************************************\n");
			print_runq_credit_stats(best_kt->active_runq, "Runqueue stolen from");
			print_runq_credit_stats(curr_kt->active_runq, "Runqueue added to");
		}
			*/
	}
}




/**********************************************************************/
/* Exported runqueue operations */
extern void init_runqueue(runqueue_t *runq)
{
	uthread_head_t *uhead;
	int i, j;
	/* Everything else is global, so already initialized to 0(correct init value) */
	for(i=0; i<MAX_UTHREAD_PRIORITY; i++)
	{
		for(j=0; j<MAX_UTHREAD_GROUPS; j++)
		{
			uhead = &((runq)->prio_array[i].group[j]);
			TAILQ_INIT(uhead);
		}
	}
	return;
}

extern void add_to_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x02;
	if (enable_lb) {
		print_runq_credit_stats(runq, "BEFORE ADDING TO EMPTY QUEUE (load balancing)");
		__add_to_runqueue(runq, u_elem);
		print_runq_credit_stats(runq, "AFTER ADDING TO EMPTY QUEUE (load balancing)");
	} else {
		__add_to_runqueue(runq, u_elem);
	}
	gt_spin_unlock(runq_lock);
	return;
}

extern void rem_from_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x03;
	__rem_from_runqueue(runq, u_elem);
	gt_spin_unlock(runq_lock);
	return;
}

extern void switch_runqueue(runqueue_t *from_runq, gt_spinlock_t *from_runqlock, 
		runqueue_t *to_runq, gt_spinlock_t *to_runqlock, uthread_struct_t *u_elem)
{
	rem_from_runqueue(from_runq, from_runqlock, u_elem);
	add_to_runqueue(to_runq, to_runqlock, u_elem);
	return;
}


/**********************************************************************/

extern void kthread_init_runqueue(kthread_runqueue_t *kthread_runq)
{
	kthread_runq->active_runq = &(kthread_runq->runqueues[0]);
	kthread_runq->expires_runq = &(kthread_runq->runqueues[1]);

	gt_spinlock_init(&(kthread_runq->kthread_runqlock));
	init_runqueue(kthread_runq->active_runq);
	init_runqueue(kthread_runq->expires_runq);

	TAILQ_INIT(&(kthread_runq->zombie_uthreads));
	return;
}

static void print_runq_stats(runqueue_t *runq, char *runq_str)
{
	int inx;
	printf("******************************************************\n");
	printf("Run queue(%s) state : \n", runq_str);
	printf("******************************************************\n");
	printf("uthreads details - (tot:%d , mask:%x)\n", runq->uthread_tot, runq->uthread_mask);
	printf("******************************************************\n");
	printf("uthread priority details : \n");
	for(inx=0; inx<MAX_UTHREAD_PRIORITY; inx++)
		printf("uthread priority (%d) - (tot:%d)\n", inx, runq->uthread_prio_tot[inx]);
	printf("******************************************************\n");
	printf("uthread group details : \n");
	for(inx=0; inx<MAX_UTHREAD_GROUPS; inx++)
		printf("uthread group (%d) - (tot:%d , mask:%x)\n", inx, runq->uthread_group_tot[inx], runq->uthread_group_mask[inx]);
	printf("******************************************************\n");
	return;
}

extern uthread_struct_t *sched_find_best_uthread(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find the highest priority RUNNABLE uthread in active-runq.
	 * [2] Found - Jump to [FOUND]
	 * [3] Switches runqueues (active/expires)
	 * [4] Repeat [1] through [2]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */

	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup;

	gt_spin_lock(&(kthread_runq->kthread_runqlock));

	runq = kthread_runq->active_runq;

	kthread_runq->kthread_runqlock.holder = 0x04;
	if(!(runq->uthread_mask))
	{ /* No jobs in active. switch runqueue */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->expires_runq;
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
			return NULL;
		}
	}

	/* Find the highest priority bucket */
	uprio = LOWEST_BIT_SET(runq->uthread_mask);
	prioq = &(runq->prio_array[uprio]);

	assert(prioq->group_mask);
	ugroup = LOWEST_BIT_SET(prioq->group_mask);

	u_head = &(prioq->group[ugroup]);
	u_obj = TAILQ_FIRST(u_head);
	__rem_from_runqueue(runq, u_obj);

	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
#if 0
	printf("cpu(%d) : sched best uthread(id:%d, group:%d)\n", u_obj->cpu_id, u_obj->uthread_tid, u_obj->uthread_gid);
#endif
	return(u_obj);
}



/* XXX: More work to be done !!! */
extern gt_spinlock_t uthread_group_penalty_lock;
extern unsigned int uthread_group_penalty;

extern uthread_struct_t *sched_find_best_uthread_group(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find a RUNNABLE uthread in active-runq from u_gid.
	 * [2] Found - Jump to [FOUND]
	 * [3] Tries to find a thread from a group with least threads in runq (XXX: NOT DONE)
	 * - [Tries to find the highest priority RUNNABLE thread (XXX: DONE)]
	 * [4] Found - Jump to [FOUND]
	 * [5] Switches runqueues (active/expires)
	 * [6] Repeat [1] through [4]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */
	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup, mask;
	uthread_group_t u_gid;

#ifndef COSCHED
	return sched_find_best_uthread(kthread_runq);
#endif

	/* XXX: Read u_gid from global uthread-select-criterion */
	u_gid = 0;
	runq = kthread_runq->active_runq;

	if(!runq->uthread_mask)
	{ /* No jobs in active. switch runqueue */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->expires_runq;
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			return NULL;
		}
	}


	if(!(mask = runq->uthread_group_mask[u_gid]))
	{ /* No uthreads in the desired group */
		assert(!runq->uthread_group_tot[u_gid]);
		return (sched_find_best_uthread(kthread_runq));
	}

	/* Find the highest priority bucket for u_gid */
	uprio = LOWEST_BIT_SET(mask);

	/* Take out a uthread from the bucket. Return it. */
	u_head = &(runq->prio_array[uprio].group[u_gid]);
	u_obj = TAILQ_FIRST(u_head);
	rem_from_runqueue(runq, &(kthread_runq->kthread_runqlock), u_obj);

	return(u_obj);
}

void print_runq_credit_stats(runqueue_t *runq, char *runq_str) {
	int inx;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_curr, *best_thread = NULL;
	unsigned int uprio, ugroup;

	printf("******************************************************\n");
	printf("Run queue CREDIT (%s) state : \n", runq_str);
	printf("******************************************************\n");

	if (!runq) {
		printf("\n Runqueue is EMPTY!!!!!!\n");
		return;
	}

	unsigned int u_t_mask = runq->uthread_mask;
	while (u_t_mask) {
		uprio = LOWEST_BIT_SET(u_t_mask); // allows to only pay attention to the relevant entries
		u_t_mask &= ~(1 << uprio);
		prioq = &(runq->prio_array[uprio]);
		unsigned int g_t_mask = prioq->group_mask;
		while (g_t_mask) {
			ugroup = LOWEST_BIT_SET(g_t_mask); // we only look at the groups with "priority level" 'uprio'
			g_t_mask &= ~(1 << ugroup);
			u_head = &(prioq->group[ugroup]);
			u_curr = TAILQ_FIRST(u_head);
			while (u_curr != NULL) {
				printf(
					"uthread_id (%d), uthread_gid (%d), uthread_credits (%ld)\n",
					u_curr->uthread_tid,
					u_curr->uthread_gid,
					u_curr->uthread_credits
				);
				u_curr = TAILQ_NEXT(u_curr, uthread_runq);
			}
		}
	}
}

extern uthread_struct_t *sched_find_best_uthread_credit(kthread_runqueue_t *kthread_runq) {
	//printf("\n let's see are we actually in this????\n");

	runqueue_t *runq;
	prio_struct_t *prioq;
	//uthread_head_t *u_head;
	uthread_struct_t *u_curr, *u_best = NULL;
	unsigned int uprio, ugroup;
	struct timeval curr_time;
	long most_creds = 0;

	//FOR TESTING
	/*
	gt_spin_lock(&(kthread_runq->kthread_runqlock));
	runqueue_t *expired = kthread_runq->expires_runq;
	int total_expired = expired->uthread_tot;
	printf("\nTHE TOTAL NUMBER OF EXPIRED THREADS IS: (%d)\n", total_expired);
	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
	*/

	gt_spin_lock(&(kthread_runq->kthread_runqlock));

	runq = kthread_runq->active_runq;
	if (!runq->uthread_mask) {
		//printf("DID WE ENTER HERE??????????? LETS SEE IF WE DID!!!!!!\n\n\n");
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->active_runq;
		if(!runq->uthread_mask) {
			assert(!runq->uthread_tot);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
			return NULL;
		}

		//printf("WE AHVE ENTERED INSIDE OF THE REFRESH CREDITS FOR ALL !!!!!!!!\n"); // Added: FOR DEBUGGING PURPOSES

		//now that we have the expired runq as our active runq, we need to go through it because our expired runq is supposed to contain ALL negative values
			//how to enforce??? ==> for every uthread in our expired queue, we want to reset "uthread_credits" and move them back to active queue (SWITCH)
		
		unsigned int uthread_mask = runq->uthread_mask;
		while (uthread_mask) {
			uprio = LOWEST_BIT_SET(uthread_mask);
			uthread_mask &= ~(1 << uprio);
			prioq = &(runq->prio_array[uprio]);

			//assert(prioq->group_mask); LETS LEAVE IT WITHOUT FIRST AND DO A DIRECT CHECK
			if (!prioq->group_mask) {
				continue;
			}
			unsigned int group_mask = prioq->group_mask;

			while(group_mask) {
				ugroup = LOWEST_BIT_SET(group_mask);
				group_mask &= ~(1 << ugroup);
				u_curr = TAILQ_FIRST(&(prioq->group[ugroup]));
				while (u_curr) {
					/*
					printf("INITIAL CREDIT TIME FOR THIS THREAD: (%ld)\n", u_curr->uthread_init_credits);
					printf("CURRENT CREDITS LEFT FOR CURR THREAD: (%ld)\n", u_curr->uthread_credits);
					printf("IS_OVER????: %d\n\n", u_curr->is_over);
					*/

					u_curr->is_over = 0;
					u_curr->uthread_credits = u_curr->uthread_init_credits;
					u_curr = TAILQ_NEXT(u_curr, uthread_runq);
					
					/*
					printf("\nWE AHVE ENTERED INSIDE OF THE REFRESH CREDITS FOR ALL !!!!!!!!\n"); // Added: FOR DEBUGGING PURPOSES
					printf("\nINITIAL CREDIT TIME FOR THIS THREAD: (%ld)\n", u_curr->uthread_init_credits);
					printf("\nCURRENT CREDITS LEFT FOR CURR THREAD: (%ld)\n", u_curr->uthread_credits);
					printf("\nIS_OVER????L %d\n\n", u_curr->is_over);
					*/
				}
			}
		}
	}

	// this is to go through each of the threads that exist and look through their credits, find max, and queue it
	//printf("******************************************************\n");
	//printf("******************************************************\n");
	unsigned int uthread_mask = runq->uthread_mask;
	//int exist_pos = 0; // Added: for testing !!!!!
	while (uthread_mask) {
		uprio = LOWEST_BIT_SET(uthread_mask);
		uthread_mask &= ~(1 << uprio);
		prioq = &(runq->prio_array[uprio]);

		//assert(prioq->group_mask); LETS LEAVE IT WITHOUT FIRST AND DO A DIRECT CHECK
		if (!prioq->group_mask) {
			continue;
		}
		unsigned int group_mask = prioq->group_mask;

		while(group_mask) {
			ugroup = LOWEST_BIT_SET(group_mask);
			group_mask &= ~(1 << ugroup);
			u_curr = TAILQ_FIRST(&(prioq->group[ugroup]));
			while (u_curr) {
				//long elapsed_time = ((curr_time.tv_sec - u_curr->start_time.tv_sec) * 1000L) + ((curr_time.tv_usec - u_curr->start_time.tv_usec) / 1000L);
				/*
				printf(
					"uthread_id (%d), uthread_gid (%d), uthread_credits (%ld)\n",
					u_curr->uthread_tid,
					u_curr->uthread_gid,
					u_curr->uthread_credits
				);
				*/
				if ((u_curr->uthread_credits > most_creds) && !(u_curr->is_over)) { // if this thread has new most creds and is NOT over (<=0)
					//exist_pos = 1; // Added: for testing !!!!
					u_best = u_curr;
					most_creds = u_curr->uthread_credits;
				}
				u_curr = TAILQ_NEXT(u_curr, uthread_runq);
			}
		}
	}

	if (u_best) {
		__rem_from_runqueue(runq, u_best); // we move it from the current runqueue, in preparation to run it (WHEREVER WE CALL this function, check that it is then marked as RUNNING !!!!!!)
	}
	//printf("\n\n\nThread id: (%d), Group id: (%d), Number of credits (scheduled): (%ld)\n\n\n", u_best->uthread_tid, u_best->uthread_gid, u_best->uthread_credits);
	//printf("Thread id: (%d)", u_best->uthread_tid);
	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
	return u_best;
}

#if 0
/*****************************************************************************************/
/* Main Test Function */

runqueue_t active_runqueue, expires_runqueue;

#define MAX_UTHREADS 1000
uthread_struct_t u_objs[MAX_UTHREADS];

static void fill_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* create and insert */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		u_obj->uthread_tid = inx;
		u_obj->uthread_gid = (inx % MAX_UTHREAD_GROUPS);
		u_obj->uthread_priority = (inx % MAX_UTHREAD_PRIORITY);
		__add_to_runqueue(runq, u_obj);
		printf("Uthread (id:%d , prio:%d) inserted\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

static void change_runq(runqueue_t *from_runq, runqueue_t *to_runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		switch_runqueue(from_runq, to_runq, u_obj);
		printf("Uthread (id:%d , prio:%d) moved\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}


static void empty_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		__rem_from_runqueue(runq, u_obj);
		printf("Uthread (id:%d , prio:%d) removed\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

int main()
{
	runqueue_t *active_runq, *expires_runq;
	uthread_struct_t *u_obj;
	int inx;

	active_runq = &active_runqueue;
	expires_runq = &expires_runqueue;

	init_runqueue(active_runq);
	init_runqueue(expires_runq);

	fill_runq(active_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");
	change_runq(active_runq, expires_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");
	empty_runq(expires_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");

	return 0;
}

#endif
