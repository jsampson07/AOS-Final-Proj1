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
#include <string.h>

#include "gt_include.h"

#define NUM_CPUS 2 // before 2
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP_COLS (SIZE/NUM_GROUPS)

#define NUM_THREADS 128 //changed from 32 to 128 for experiment
#define PER_THREAD_ROWS (SIZE/NUM_THREADS)


// Used for experiments
//#define NUM_UTHREADS 128
#define NUM_CREDIT_GROUPS 4
#define NUM_MATRIX_SIZES 4
#define MAX_MATRIX_SIZE 256
#define NUM_THREADS_COMBO 8
#define PER_GROUP_EXP 16


#define ROWS MAX_MATRIX_SIZE
#define COLS ROWS
#define SIZE COLS

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

uthread_struct_t *all_uthreads[NUM_THREADS];

typedef struct matrix
{
	int m[SIZE][SIZE];

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;

	int credits;
	int matrix_size;
	//int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	//int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	
}uthread_arg_t;

struct timeval tv1;
int is_credit_scheduler = 0;
int enable_yield = 0;
int enable_lb = 0;
const int credit_groups[NUM_CREDIT_GROUPS] = {25, 50, 75, 100};
const int matrix_sizes[NUM_MATRIX_SIZES] = {32, 64, 128, 256};

static void generate_matrix(matrix_t *mat, int val)
{

	int i,j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{
			mat->m[i][j] = val;
		}
	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for(i=0;i<SIZE;i++)
	{
		for(j=0;j<SIZE;j++)
			printf(" %d ",mat->m[i][j]);
		printf("\n");
	}

	return;
}

extern int uthread_create(uthread_t *, void *, void *, uthread_group_t, int);

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	/*

	start_row = ptr->start_row;
	end_row = (ptr->start_row + PER_THREAD_ROWS);

#ifdef GT_GROUP_SPLIT
	start_col = ptr->start_col;
	end_col = (ptr->start_col + PER_THREAD_ROWS);
#else
	start_col = 0;
	end_col = SIZE;
#endif

*/

#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	//fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	//fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif

	/*
	for(i = start_row; i < end_row; i++)
		for(j = start_col; j < end_col; j++)
			for(k = 0; k < SIZE; k++)
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];
	*/

	//col and row are both SIZE long in generate_matrix()
	//This ensures that every thread does its FULL matrix mult rather than just "its" row and col

	//FOR EXPERIMENT: not all uthreads have the same matrix size
	int matrix_size = ptr->matrix_size;
	int yield_condition = matrix_size / 2;
	for (i = 0; i < matrix_size; i++) {
		if ((i == yield_condition) && enable_yield) { // this is where i yield
			uthread_struct_t *this_uthread = kthread_cpu_map[kthread_apic_id()]->krunqueue.cur_uthread; // find the current uthread running
			struct timeval curr_time;
			gettimeofday(&curr_time, NULL);
			/* i added this before call to gt_yield because it didnt make sense to use original version:
					print credits the thread started running with because that tells us nothing,
					and i wanted to treat gt_yield() as the actual beginning of the "yielding" process
						even tho the thread technically yields after it is set to UTHREAD_RUNNABLE inside uthread_schedule
						--> basically just for print results - will mention slight discrepancy in the report */
			long elapsed_time = (curr_time.tv_sec - this_uthread->start_time.tv_sec) * 1000000L + (curr_time.tv_usec - this_uthread->start_time.tv_usec);
			long credits_before_yield = this_uthread->uthread_credits - elapsed_time;

			printf("\nYielding thread: (%d), Credits (BEFORE): (%ld)\n", this_uthread->uthread_tid, credits_before_yield);
			gt_yield(this_uthread, enable_yield);
			printf("\nThread (%d) running, Credits (AFTER): (%ld)\n", this_uthread->uthread_tid, this_uthread->uthread_credits);
		}
		for (j = 0; j < matrix_size; j++) {
			for (k = 0; k < matrix_size; k++) {
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];
			}
		}
	}


#ifdef GT_THREADS
	/*fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));*/
#else
	gettimeofday(&tv2,NULL);
	/*fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));*/
#endif

#undef ptr
	return 0;
}

matrix_t A, B;
matrix_t C[NUM_THREADS];

static void init_matrices()
{
	//printf("WE IN BABY\n");
	generate_matrix(&A, 1);
	generate_matrix(&B, 1);
	//generate_matrix(&C, 0);
	for (int i = 0; i < NUM_THREADS; i++) {
		generate_matrix(&C[i], 0);
	}

	return;
}


void parse_args(int argc, char* argv[])
{
	int inx;

	for(inx=0; inx<argc; inx++)
	{
		if(argv[inx][0]=='-')
		{
			if(!strcmp(&argv[inx][1], "lb"))
			{
				//TODO: add option of load balancing mechanism
				enable_lb = 1;
				printf("enable load balancing\n");
			}
			else if(!strcmp(&argv[inx][1], "s"))
			{
				//TODO: add different types of scheduler
				inx++;
				if(!strcmp(&argv[inx][0], "0"))
				{
					is_credit_scheduler = 0;
					printf("use priority scheduler\n");
				}
				else if(!strcmp(&argv[inx][0], "1"))
				{
					is_credit_scheduler = 1;
					printf("use credit scheduler\n");
				}
			} else if(!strcmp(&argv[inx][1], "y")) {
				enable_yield = 1;
				printf("oh yeah we are on (YIELD!!!!!)\n");
			}
		}
	}

	return;

}



uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char *argv[])
{
	uthread_arg_t *uarg;
	int inx = 0;

	parse_args(argc, argv);

	gtthread_app_init();

	init_matrices();

	gettimeofday(&tv1,NULL);

	for (int c = 0; c < NUM_CREDIT_GROUPS; c++) { // first we create uthreads for credit group 25
		for (int s = 0; s < NUM_MATRIX_SIZES; s++) { // then for each credit group, we create matrix sizes 32, 64, 128, and 256 for them (at a time)
			for (int t = 0; t < NUM_THREADS_COMBO; t++) {
				uarg = &uargs[inx];
				uarg->_A = &A;
				uarg->_B = &B;
				uarg->_C = &C[inx];

				uarg->tid = inx;

				uarg->gid = (inx % NUM_GROUPS);

				uarg->credits = credit_groups[c]; /* before we did it for start_row and start_col
				now we want to set the new fields we added that are relevant */
				uarg->matrix_size = matrix_sizes[s];

				uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, uarg->credits);
				inx++;
			}
		}
	}

	/*

	for(inx=0; inx<NUM_THREADS; inx++)
	{
		uarg = &uargs[inx];
		uarg->_A = &A;
		uarg->_B = &B;
		uarg->_C = &C[inx];

		uarg->tid = inx;

		uarg->gid = (inx % NUM_GROUPS);

		THIS IS WHERE "each thread does some work on the matrix" problem arises
		uarg->start_row = (inx * PER_THREAD_ROWS);
#ifdef GT_GROUP_SPLIT
		Wanted to split the columns by groups !!!
		uarg->start_col = (uarg->gid * PER_GROUP_COLS);
#endif
		
		uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid);
	}
	*/

	gtthread_app_exit();

	// Now all threads have finished executing we create the csv files
	FILE *csv = fopen("Detailed_output.csv", "w");
	if (!csv) {
		return -1;
	}
	fprintf(csv, "group_name,thread_number,cpu_time(us),wait_time(us),exec_time(us)\n"); // this is the header row
	for (int i = 0; i < NUM_THREADS; i++) {
		uthread_struct_t *curr_thread = all_uthreads[i];
		//printf("%")
		int credits = (int) curr_thread->uthread_exact_credits;
		uthread_arg_t *this_uarg = &uargs[i]; // only way to get current matrix size
		int matrix_size = this_uarg->matrix_size;

		char c_m[12]; // create the string formatted in the way it is listed in project description
		snprintf(c_m, sizeof(c_m), "c_%d_m_%d", credits, matrix_size);
		
		int tid = curr_thread->uthread_tid;
		//long cpu_time = curr_thread->total_cpu_time;
		long cpu_time = curr_thread->total_cpu_time;
		//printf("Thread id: (%d), Total_CPU_Time: (%ld)", curr_thread->uthread_tid, curr_thread->total_cpu_time);
		//long exec_time = (curr_thread->time_completed.tv_sec - curr_thread->time_created.tv_sec)*1000000L + (curr_thread->time_completed.tv_usec - curr_thread->time_created.tv_usec); // seconds -> 1000 milliseconds; 1 millisecond -> 1000 microseconds
		long exec_time = curr_thread->exec_time;
		long wait_time = exec_time - cpu_time;

		fprintf(
			csv, "%s,%d,%ld,%ld,%ld\n",
			c_m,
			tid,
			cpu_time,
			wait_time,
			exec_time
		);
	}
	fclose(csv);

	//Per group basis so:
	/*
	(4)
	25 creds 32 size (group0)
	25 creds 64 size (group1)
	...
	(4)
	50 creds 32 size (group4)
	50 creds 64 size (group5)
	...
	(4)
	75 creds 32 size (group8)
	75 creds 64 size (group9)
	...
	(4)
	100 creds 32 size (group 12)
	100 creds 64 size (group 13)
	... (group 15 is last one)
	==>16 total
	*/
	FILE *csv_v2 = fopen("Cummulative_output.csv", "w");
	if (!csv_v2) {
		return -1;
	}

	long sum_of_cpu_times[16] = {};
	long sum_of_wait_times[16] = {};
	long sum_of_execute_times[16] = {};
	// first we will sum all of the data, and then after we will calculate averages
	for (int i = 0; i < NUM_THREADS; i++) {
		int c_indx;
		// let's find the credit group
		for (int c_group = 0; c_group < NUM_CREDIT_GROUPS; c_group++) {
			if (uargs[i].credits == credit_groups[c_group]) {
				c_indx = c_group;
				break;
			}
		}
		int s_indx;
		// let's find the size group
		for (int s_group = 0; s_group < NUM_MATRIX_SIZES; s_group++) {
			if (uargs[i].matrix_size == matrix_sizes[s_group]) {
				s_indx = s_group;
				break;
			}
		}
		int array_index = c_indx * NUM_MATRIX_SIZES + s_indx;
		uthread_struct_t *curr_thread = all_uthreads[i];
		long cpu_time = all_uthreads[i]->total_cpu_time;
		//long exec_time = (curr_thread->time_completed.tv_sec - curr_thread->time_created.tv_sec)*1000000L + (curr_thread->time_completed.tv_usec - curr_thread->time_created.tv_usec); // seconds -> 1000 milliseconds; 1 millisecond -> 1000 microseconds
		long exec_time = all_uthreads[i]->exec_time;
		long wait_time = exec_time - cpu_time;

		sum_of_cpu_times[array_index] += cpu_time;
		sum_of_execute_times[array_index] += exec_time;
		sum_of_wait_times[array_index] += wait_time;
	}

	/* scratch this idea, let's break it up into separate loops, one to sum and then to average
	for (int i = 0; i < NUM_UTHREADS; i++) {
		for (int cred_group = 0; cred_group < NUM_CREDIT_GROUPS; cred_group++) {
			for (int size_group = 0; size_group < NUM_MATRIX_SIZES; size_group++) {

			}
		}
	}
	*/

	fprintf(csv_v2, "group_name,mean_cpu_time,mean_wait_time,mean_exec_time\n");
	// now we have to go through and calculate the averages for each group i.e. c_25_m_32...
	for (int i = 0; i < PER_GROUP_EXP; i++) {
		int c_indx = i / NUM_MATRIX_SIZES;
		int s_indx = i % NUM_MATRIX_SIZES;
		// same as in the first set of data collected (loop)
		char c_m[12]; // create the string formatted in the way it is listed in project description
		snprintf(c_m, sizeof(c_m), "c_%d_m_%d", credit_groups[c_indx], matrix_sizes[s_indx]);

		long mean_cpu_time = sum_of_cpu_times[i] / NUM_THREADS_COMBO;
		long mean_execute_time = sum_of_execute_times[i] / NUM_THREADS_COMBO;
		long mean_wait_time = sum_of_wait_times[i] / NUM_THREADS_COMBO;
		fprintf(
			csv_v2, "%s,%ld,%ld,%ld\n",
			c_m,
			mean_cpu_time,
			mean_wait_time,
			mean_execute_time
		);
	}
	fclose(csv_v2);

	//printf("\nWe have successfully written to Detailed_output.csv and Cummulative_output.csv now!!!\n");

	// print_matrix(&C);
	// fprintf(stderr, "********************************");
	return(0);
}
