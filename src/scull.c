#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "scull.h"

#define CDEV_NAME "/dev/scull"

#include <errno.h>
#include <string.h>
#include <wait.h>
#include <pthread.h>

/* Quantum command line option */
static int g_quantum;

// store multi-process command line option
static int num_procs;

// store multi-thread command line option
static int num_threads;

static void usage(const char *cmd)
{
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  R          Reset quantum\n"
	       "  S <int>    Set quantum\n"
	       "  T <int>    Tell quantum\n"
	       "  G          Get quantum\n"
	       "  Q          Qeuery quantum\n"
	       "  X <int>    Exchange quantum\n"
	       "  H <int>    Shift quantum\n"
	       "  K          Return task info\n"
	       "  p <n>      Return task info for 0<n<11 processes\n"
	       "  t <n>      Return task info for 0<n<11 threads\n"
	       "  h          Print this message\n",
	       cmd);
}

typedef int cmd_t;

// print function for task_info struct
void print_task_info(task_info *ti) {
	printf(
		"state %ld, "
		"stack %lx, "
		"cpu %i, "
		"prio %d, "
		"sprio %d, "
		"nprio %d, "
		"rtprio %i, "
		"pid %ld, "
		"tgid %ld, "
		"nv %lu, "
		"niv %lu\n",
		ti->state,
		(unsigned long)ti->stack,
		ti->cpu,
		ti->prio,
		ti->static_prio,
		ti->normal_prio,
		ti->rt_priority,
		(long)ti->pid,
		(long)ti->tgid,
		ti->nvcsw,
		ti->nivcsw
	);
}

// function to call ioctl function from threads
void *scull_iockquantum(void *arg) {
	// cast arg back to fd
	int fd = *(int*)arg;
	
	// call ioctl function and print ti
	task_info *ti = (task_info*)malloc(sizeof(task_info));
	if (ioctl(fd, SCULL_IOCKQUANTUM, ti) < 0) {
		fprintf(stderr, "Error: SCULL_IOCKQUANTUM call failed. %s\n", strerror(errno));
		goto exit;
	}
	print_task_info(ti);

	// free ti and exit function
exit:
	free(ti);
	pthread_exit(NULL);
}

static cmd_t parse_arguments(int argc, const char **argv)
{
	cmd_t cmd;

	if (argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch (cmd) {
	case 'S':
	case 'T':
	case 'H':
	case 'X':
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		break;
	case 'R':
	case 'G':
	case 'Q':
	
	// normal call to task_info ioctl function
	case 'K':
		break;
	// multi-process call to task_info ioctl function
	case 'p':
		// throw error if process count is missing
		if (argc < 3) {
			fprintf(stderr, "%s: Missing process count.\n", argv[0]);
			cmd = -1;
			break;	
		}
		
		// store process count
		num_procs = atoi(argv[2]);
		
		// throw error if process count is out of range
		if (num_procs < 1 || num_procs > 10) {
			fprintf(stderr, "%s: Enter a process count from 1-10.\n", argv[0]);
			cmd = -1;
			break;
		}
		break;

	// multi-thread call to task_info ioctl function
	case 't':
		// throw error if thread count is missing
		if (argc < 3) {			
			fprintf(stderr, "%s: Missing thread count.\n", argv[0]);
			cmd = -1;
			break;
		}
		
		// store thread count
		num_threads = atoi(argv[2]);
		
		// throw error if thread count is out of range
		if (num_threads < 1 || num_threads > 10) {
			fprintf(stderr, "%s: Enter a thread count from 1-10.\n", argv[0]);
			cmd = -1;
			break;
		}
		break;

	case 'h':
		break;
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if (cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
}

static int do_op(int fd, cmd_t cmd)
{
	int ret = 0, q;
	task_info *ti;
	pthread_t *threads;
	int thread_status;
	
	switch (cmd) {
	case 'R':
		ret = ioctl(fd, SCULL_IOCRESET);
		if (ret == 0)
			printf("Quantum reset\n");
		break;
	case 'Q':
		q = ioctl(fd, SCULL_IOCQQUANTUM);
		printf("Quantum: %d\n", q);
		ret = 0;
		break;
	case 'G':
		ret = ioctl(fd, SCULL_IOCGQUANTUM, &q);
		if (ret == 0)
			printf("Quantum: %d\n", q);
		break;
	case 'T':
		ret = ioctl(fd, SCULL_IOCTQUANTUM, g_quantum);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'S':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCSQUANTUM, &q);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'X':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCXQUANTUM, &q);
		if (ret == 0)
			printf("Quantum exchanged, old quantum: %d\n", q);
		break;
	case 'H':
		q = ioctl(fd, SCULL_IOCHQUANTUM, g_quantum);
		printf("Quantum shifted, old quantum: %d\n", q);
		ret = 0;
		break;	
	
	// normal call to task_info iotcl function
	case 'K':
		// call ioctl function and print ti
		ti = (task_info*)malloc(sizeof(task_info));	
		if ((ret = ioctl(fd, SCULL_IOCKQUANTUM, ti)) < 0) {
			fprintf(stderr, "Error: SCULL_IOCKQUANTUM call failed. %s\n", 
				strerror(errno));
			break;
		}
		print_task_info(ti);
		
		// free ti and exit case
		free(ti);
		break;

	// multi-process call to task_info ioctl function
	case 'p':
		ti = (task_info*)malloc(sizeof(task_info));
		for (int i = 0; i < num_procs; i++) {
			pid_t pid;
			pid_t w;
			
			// create new process
			if ((pid = fork()) < 0) {
				fprintf(stderr, "Error: fork[%d] failed. %s.\n", 
					i+1, strerror(errno));
				continue;
			} else if (pid == 0) {
				// call ioctl function and print ti in child
				if (ioctl(fd, SCULL_IOCKQUANTUM, ti) < 0) {
					fprintf(stderr, 
						"Error: SCULL_IOCKQUANTUM call[%d] failed. %s\n", 
						i+1, strerror(errno));
					continue;
				}
				print_task_info(ti);
				exit(0);
			} else {	
				// wait for child to finish in parent
				if ((w = wait(NULL)) < 0) {
					fprintf(stderr, "Error: wait[%d] failed. %s\n", 
						i+1, strerror(errno));
				
					continue;
				}
			}
		}
		
		// free ti and exit case
		free(ti);
		break;

	// multi-thread call to task_info ioctl function
	case 't':
		// create threads
		threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
		for (int i = 0; i < num_threads; i++) {
			if ((thread_status = pthread_create(
						&threads[i], 
						NULL, 
						scull_iockquantum, 
						(void*)&fd)
				) != 0) {
				fprintf(stderr, "Error: pthread_create[%d] failed. %s\n", i+1, 
					strerror(thread_status));
			}
		}

		// wait for each thread to finish
		for (int i = 0; i < num_threads; i++) {
			if ((thread_status = pthread_join(threads[i], NULL)) != 0) {
				fprintf(stderr, "Warning: pthread_join[%d] failed. %s\n", i+1,
					strerror(thread_status));
			}
		}
		
		// free threads and exit case		
		free(threads);
		break;

	default:
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}

	if (ret != 0)
		perror("ioctl");
	return ret;
}

int main(int argc, const char **argv)
{
	int fd, ret;
	cmd_t cmd;

	cmd = parse_arguments(argc, argv);

	fd = open(CDEV_NAME, O_RDONLY);
	if (fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);

	ret = do_op(fd, cmd);

	if (close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}
