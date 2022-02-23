// Virtual Choir Rehearsal Room  Copyright (C) 2021  Lukas Ondracek <ondracek.lukas@gmail.com>, use under GNU GPLv3


#include <sched.h>
#include <unistd.h>

#ifdef SERVER_SCHED_DEADLINE
#warning Defining sched_*attr, remove these lines if already defined

#define gettid() syscall(__NR_gettid)

#define SCHED_DEADLINE	6
#define SCHED_FLAG_RESET_ON_FORK 0x01

/* XXX use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setattr		314
#define __NR_sched_getattr		315
#endif

#ifdef __i386__
#define __NR_sched_setattr		351
#define __NR_sched_getattr		352
#endif

#ifdef __arm__
#define __NR_sched_setattr		380
#define __NR_sched_getattr		381
#endif

//static volatile int done;

struct sched_attr {
uint32_t size;

uint32_t sched_policy;
uint64_t sched_flags;

/* SCHED_NORMAL, SCHED_BATCH */
int32_t sched_nice;

/* SCHED_FIFO, SCHED_RR */
uint32_t sched_priority;

/* SCHED_DEADLINE (nsec) */
uint64_t sched_runtime;
uint64_t sched_deadline;
uint64_t sched_period;
};

int sched_setattr(pid_t pid,
		const struct sched_attr *attr,
		unsigned int flags)
{
return syscall(__NR_sched_setattr, pid, attr, flags);
}

int sched_getattr(pid_t pid,
		struct sched_attr *attr,
		unsigned int size,
		unsigned int flags)
{
return syscall(__NR_sched_getattr, pid, attr, size, flags);
}


bool threadPriorityDeadline(uint64_t runtime, uint64_t deadline, uint64_t period) {
	struct sched_attr sched = {};
	//sched_getattr(0, &sched, sizeof(sched), 0);
	sched.sched_policy = SCHED_DEADLINE;
	sched.sched_runtime = runtime;
	sched.sched_deadline = deadline;
	sched.sched_period = period;
	sched.sched_flags = SCHED_FLAG_RESET_ON_FORK;
	if (sched_setattr(0, &sched, 0) != 0) {
		switch (errno) {
			case EBUSY:
				printf("Cannot set deadline priority, not enough CPU time.\n");
				return false;
			default:
				printf("Cannot set deadline priority (CAP_SYS_NICE needed).\n");
				return false;
		}
	}
	return true;
}

#else

bool threadPriorityDeadline(uint64_t runtime, uint64_t deadline, uint64_t period) {
	return false;
}

#endif

bool threadPriorityRealtime(uint32_t priority) {
	struct sched_param param;
	param.sched_priority = priority;
	if (sched_setscheduler(0, SCHED_RR | SCHED_RESET_ON_FORK, &param) == 0) {
		return true;
	} else {
		printf("Cannot set realtime priority (CAP_SYS_NICE needed).\n");
		return false;
	}
}

bool threadPriorityNice(uint32_t inc) {
	errno = 0;
	if ((nice(inc) != -1) || (errno == 0)) {
		return true;
	} else {
		printf("Cannot increase nice level of thread.\n");
		return false;
	}
}
