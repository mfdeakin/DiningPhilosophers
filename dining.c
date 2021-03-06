
/* Dining Philosophers
 * By Michael Deakin
 * CSC 460
 * /home/deakin_mf/csc460/hw/dining
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <unistd.h>

#define NUMPHILOSOPHERS 5
#define TIMELIMIT 100

enum states {
	THINKING = 0,
	HUNGRY = 1,
	EATING = 2
};

struct philosophers {
	int shmid;		//The id of the shared memory
	int running;	//Number of philosophers which are still running
	int runmtx;		//Mutex for updating the number of running processes
	int testmtx;	//Mutex for updating the state
	int statemtx;	//Mutex for updating the "utensils"
	time_t maxtime;	//The minimum end time for the philosophers
	int count;	//The number of philosophers
	/* Variably sized array ahead */
	enum states state[];	//The state of the i'th philosopher
};

struct philosophers *initPhilosophers(int count, int timelimit);
void freePhilosophers(struct philosophers *);
void simulate(struct philosophers *);
int left(struct philosophers *, int pos);
int right(struct philosophers *, int pos);
void think(void);
void eat(void);
void takeForks(struct philosophers *, int id);
void putForks(struct philosophers *, int id);
void test(struct philosophers *, int id);

int getsem(int count, int initial);
void p(int semaphore, int num);
#define down p
void v(int semaphore, int num);
#define up v

void ctrlc(int sig);

/* So we can catch and cleanup CTRL-C's */
struct philosophers *phil;

int main(int argc, char **argv)
{
	/* Allow the user to optionally specify how many philosophers
	 * and the minimum amount of time to run 
	 */
	int n = NUMPHILOSOPHERS,
		t = TIMELIMIT;
	signal(SIGINT, ctrlc);
	if(argc > 1) {
		int chk = sscanf(argv[1], "%d", &n);
		if(chk == 0)
			n = NUMPHILOSOPHERS;
		if(argc > 2) {
			chk = sscanf(argv[2], "%d", &t);
			if(chk == 0)
				t = TIMELIMIT;
		}
	}
	phil = initPhilosophers(n, t);
	if(fork() == 0) {
		/* Child process, runs simulation */
		simulate(phil);
	}
	else {
		/* Used to map states to strings */
		const char *statestr[] = {
			"thinking",
			"hungry",
			"eating"
		};
		int runcount = 1;
		/* This is safe without a mutex because running is never set to 0
		 * unless it's finished, and we don't care if we run an extra time.
		 * The other stuff is safe because it's never set to an invalid state
		 */
		while(phil->running > 0) {
			printf("%d.\t", runcount);
			for(int i = 0; i < phil->count; i++) {
				printf("\t%8s", statestr[phil->state[i]]);
			}
			printf("\n");
			runcount++;
			sleep(1);
		}
		freePhilosophers(phil);
	}
	return 0;
}

/* Cleanup and get out of Dodge */
void ctrlc(int sig)
{
	p(phil->runmtx, 0);
	phil->running--;
	/* We're the last one, so clean up! */
	if(phil->running == -1) {
		freePhilosophers(phil);
	}
	else {
		v(phil->runmtx, 0);
	}
	exit(0);
}

struct philosophers *initPhilosophers(int count, int timelimit)
{
	size_t size = sizeof(struct philosophers) + sizeof(enum states[count]);
	int shmid = shmget(IPC_PRIVATE, size, 0660);
	if(!shmid) {
		fprintf(stderr, "Could not open shared memory!\n");
		exit(1);
	}
	struct philosophers *ph = shmat(shmid, NULL, 0);
	memset(ph, 0, sizeof(*ph));
	ph->count = count;
	ph->shmid = shmid;
	ph->running = ph->count;
	ph->maxtime = time(NULL) + timelimit;
	ph->runmtx = getsem(1, 1);
	if(ph->runmtx == -1) {
		fprintf(stderr, "Could not allocate run mutex!\n");
		freePhilosophers(ph);
		exit(1);
	}
	ph->testmtx = getsem(1, 1);
	if(ph->testmtx == -1) {
		fprintf(stderr, "Could not allocate test mutex!\n");
		freePhilosophers(ph);
		exit(1);
	}
	ph->statemtx = getsem(ph->count, 0);
	if(ph->statemtx == -1) {
		fprintf(stderr, "Could not allocate state mutex!\n");
		freePhilosophers(ph);
		exit(1);
	}
	return ph;
}

void freePhilosophers(struct philosophers *ph)
{
	if(ph->runmtx != -1)
		semctl(ph->runmtx, 0, IPC_RMID);
	if(ph->testmtx != -1)
		semctl(ph->testmtx, 0, IPC_RMID);
	if(ph->statemtx != -1)
		semctl(ph->statemtx, 0, IPC_RMID);
	shmctl(ph->shmid, IPC_RMID, NULL);
	shmdt(ph);
}

void simulate(struct philosophers *ph)
{
	/* Warning: for loop abuse.
	 * Creates ph->count - 1 more processes, assigns them a unique id
	 */
	int id;
	for(id = 0;
			id < ph->count - 1 && fork();
			id++);
	/* Initialize the RNG here so that each philosopher has it's own seed */
	srand(time(NULL) + id);
	/* Begin the philosophers routine */
	while(time(NULL) < ph->maxtime) {
		think();
		takeForks(ph, id);
		eat();
		putForks(ph, id);
	}
	/* Let the main process know there's one less process to worry about */
	p(ph->runmtx, 0);
	ph->running--;
	v(ph->runmtx, 0);
}

void takeForks(struct philosophers *ph, int id)
{
	down(ph->testmtx, 0);
	ph->state[id] = HUNGRY;
	test(ph, id);
	up(ph->testmtx, 0);
	down(ph->statemtx, id);
}

void putForks(struct philosophers *ph, int id)
{
	down(ph->testmtx, 0);
	ph->state[id] = THINKING;
	test(ph, left(ph, id));
	test(ph, right(ph, id));
	up(ph->testmtx, 0);
}

void test(struct philosophers *ph, int id)
{
	if(ph->state[id] == HUNGRY &&
		 ph->state[left(ph, id)] != EATING &&
		 ph->state[right(ph, id)] != EATING) {
		ph->state[id] = EATING;
		up(ph->statemtx, id);
	}
}

int left(struct philosophers *ph, int id)
{
	/* Add ph->count to make certain we don't go negative */
	return (id + ph->count - 1) % ph->count;
}

int right(struct philosophers *ph, int id)
{
	return (id + 1) % ph->count;
}

void think(void)
{
	sleep(rand() % 11 + 5);
}

void eat(void)
{
	sleep(rand() % 3 + 1);
}

/* Locks the critical section */
void p(int semaphore, int num)
{
	struct sembuf operations;
	operations.sem_num = num;
	operations.sem_op = -1;
	operations.sem_flg = 0;
	if(semop(semaphore, &operations, 1) == -1) {
		printf("P Error\n");
	}
}

/* Unlocks the critical section */
void v(int semaphore, int num)
{
	struct sembuf operations;
	operations.sem_num = num;
	operations.sem_op = 1;
	operations.sem_flg = 0;
	if(semop(semaphore, &operations, 1) == -1) {
		printf("V Error\n");
	}
}

/* Gets count semaphores.
 * The semaphore is initialized with the initial value
 */
int getsem(int count, int initial)
{
	int semid = semget(IPC_PRIVATE, count, 0777);
	if(semid != -1) {
		for(int i = 0; i < count; i++)
			semctl(semid, i, SETVAL, initial);
	}
	return semid;
}
