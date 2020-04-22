#define _XOPEN_SOURCE 700
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>

#define check_error(expr,userMsg)\
	do {\
		if (!(expr)) {\
			perror(userMsg);\
			exit(EXIT_FAILURE);\
		}\
	} while (0)

#define KEY 0x1111

#define MAX_RESOURSES 20U
#define MAX_PROCESSES 18U
#define NS_S 1000000000U
#define MS_S 1000U
//#define TERM_TIME 2500U
#define TERM_TIME 500U
#define TERM_CHANCE 10U

//oss msg
#define GRANTED 1U
#define DENIED 0U
#define TERMINATE 2U
//user msg
#define REQUEST 3U
#define RELEASE 1U
#define TERMINATED 2U
#define HOLD 0U
//state
#define WAITING 0U
#define HOLDING 1U

typedef struct{
    unsigned resourses[MAX_RESOURSES];
    unsigned shared_qm[MAX_RESOURSES];
    unsigned processes[MAX_PROCESSES];
    unsigned waiting[MAX_PROCESSES];
    unsigned seconds;
    unsigned nanoseconds;
} shared_mem;

typedef struct{
    sem_t sem_r;
    sem_t sem_w;
    unsigned oss_msg;
    unsigned usr_msg;
    unsigned request;
    unsigned resourses[MAX_RESOURSES];
    unsigned status;
    unsigned id;
} user_mem;

void
init_user_mem(user_mem** u_mem)
{
    sem_init(&((*u_mem)->sem_r), 1, 1);
    sem_init(&((*u_mem)->sem_w), 1, 0);
    (*u_mem)->oss_msg = 0;
    (*u_mem)->usr_msg = 0;
    (*u_mem)->status = 0;
    for(unsigned i = 0; i<MAX_PROCESSES; i++)
        ((*u_mem)->resourses)[i] = 0;
}

int sig_int = 0;

void
process_signal(int signum)
{
	switch (signum){
		case SIGINT:
            sig_int = 1 ;
			break;
        default:
            break;
	}
}


void*
get_memory_block(char* fpath, unsigned size)
{
	int memFd = shm_open(fpath, O_RDWR, 0600);
	check_error(memFd != -1, "shm_open failed");
	void* addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, memFd, 0);
	check_error(addr != MAP_FAILED, "mmap");
	close(memFd);
	return addr;
}

void
terminate(shared_mem** clock, user_mem** self_block) //cleanly terminates, notifying oss
{
    (*self_block)->usr_msg = TERMINATED;
    sem_post(&((*self_block)->sem_w));
    munmap(*clock, sizeof(shared_mem));
    munmap(*self_block, sizeof(user_mem));
    exit(EXIT_SUCCESS);
}

unsigned
resourse_n(user_mem** block) //returns number of resourses held
{
    unsigned k = 0;
    for(unsigned i=0; i<MAX_RESOURSES; i++){
        if(((*block)->resourses)[i] == 1){
            k++;
        }
    }
    return k;
}

int
main(int argc, char** argv)
{
    srand(time(NULL));
    shared_mem* clock = get_memory_block("/shared_mem", sizeof(shared_mem));
	user_mem* self_block = get_memory_block(argv[1], sizeof(user_mem));

    unsigned time_since_last_term = clock->nanoseconds;

    while(1){
        signal(SIGINT,process_signal);
        if(sig_int == 1) // terminates when oss ends
            terminate(&clock, &self_block);
        if((time_since_last_term - clock->nanoseconds) > (rand() % TERM_TIME)){//check if it should terminate
            time_since_last_term = clock->nanoseconds;
            if(rand() % 100 < TERM_CHANCE)
                terminate(&clock, &self_block);
        }
        else{
            time_since_last_term = clock->nanoseconds;
        }
        sem_wait(&(self_block->sem_r)); //waits for oss
        if(rand()%2 == 0){ //request
            if(resourse_n(&self_block) == MAX_RESOURSES){
                self_block->usr_msg = HOLD;
                sem_post(&(self_block->sem_w));
                continue; //can't request if it already has all resourses
            }
            unsigned rand_resourse = rand() % MAX_RESOURSES;
            if((self_block->resourses)[rand_resourse] == 1){
                self_block->usr_msg = HOLD;
                sem_post(&(self_block->sem_w));
                continue; //can't request if it already has that resourse
            }
            self_block->request = rand_resourse;
            self_block->usr_msg = REQUEST;
            sem_post(&(self_block->sem_w)); 
            while(1){ // waits blocked until resourse is released
                signal(SIGINT,process_signal);
                if(sig_int == 1)
                    terminate(&clock, &self_block);
                sem_wait(&(self_block->sem_r));
                if(self_block->oss_msg == TERMINATE)
                    terminate(&clock, &self_block); //terminated by deadlock resolver
                if(self_block->oss_msg == DENIED){
                    sem_post(&(self_block->sem_w));
                    continue;
                } 
                else {
                    self_block->usr_msg = HOLD;
                    sem_post(&(self_block->sem_w));
                    break;
                }
            }
        }
        else{ //release
            unsigned rand_resourse = rand() % MAX_RESOURSES;
            self_block->request = rand_resourse;
            self_block->usr_msg = RELEASE;
            sem_post(&(self_block->sem_w));
        }
    }

    return 0;
}

