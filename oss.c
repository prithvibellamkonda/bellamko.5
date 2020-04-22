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
//#define DEADLOCK_CHECK 4000U
#define DEADLOCK_CHECK 800U
//#define MAX_WAIT_BETWEEN_PROCESSES_SPAWN 2000U
#define MAX_WAIT_BETWEEN_PROCESSES_SPAWN 400U

#define MAX_LOG 10000U
unsigned log_n = 0;

//oss msg
#define GRANTED 1U
#define DENIED 0U
#define TERMINATE 2U
//user msg
#define REQUEST 3U
#define RELEASE 1U
#define TERMINATED 2U
#define HOLD 0U
//status
#define WAITING 0U
#define HOLDING 1U

#define PTR shared_mem** sh_mem, user_mem*** u_mems
#define ADR &sh_mem, &u_mems

#define VERBOSE 1
#define VRBS if (VERBOSE == 0) return

//stats
unsigned granted_n = 0;
unsigned terminated_by_deadlock_n = 0;
unsigned died_of_natural_cause_n = 0;
unsigned dd_run_n = 0;

unsigned next_id = 0;

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
clock_tick(shared_mem** sh_mem, unsigned plus)
{
    (*sh_mem)->nanoseconds += plus;
}

typedef struct{
    unsigned array[MAX_RESOURSES];
    unsigned size;
} vector;

void
vector_show(vector v)
{
    for(unsigned i=0; i<v.size; i++)
        printf("%u ", v.array[i]);
    printf("\n");
}

vector
vector_new()
{
    vector tmp;
    tmp.size=0;
    return tmp;
}

void
vector_add(vector* v, unsigned n)
{
    ((*v).array)[(*v).size++] = n;
}

int 
vector_find(vector v, unsigned n)
{
    for(unsigned i=0; i<v.size; i++){
        if((v.array)[i] == n) return i;
    }
    return -1;
}

typedef struct{
    vector cycles[MAX_PROCESSES];
    unsigned size;
} cycle;

void
cycle_show(cycle c)
{
    for(unsigned i=0; i<c.size; i++)
        vector_show(c.cycles[i]);
}

cycle
cycle_new()
{
    cycle tmp;
    tmp.size = 0;
    return tmp;
}

void
cycle_add(cycle* c, vector v){
    ((*c).cycles)[(*c).size++] = v;
}

int
cycle_find(cycle c, unsigned n){
    for(unsigned i=0; i<c.size; i++){
        int f = vector_find((c.cycles)[i], n);
        if(f != -1) return f;
    }
    return -1;
}


void
write_log(char* msg)
{
    if(log_n>MAX_LOG)return;
    FILE* log = fopen("log.log", "a");
    fprintf(log, msg);
    fclose(log);
    log_n++;
}


void
write_exit()
{
    char r[126];
    snprintf(r, 126, "Requests granted: %u, Processes terminated by deadlock/themselves: %u/%u, Deadlock runs: %u\n", granted_n, terminated_by_deadlock_n, died_of_natural_cause_n, dd_run_n);
    printf("%s", r);
    write_log(r);
}

void
write_deadlock(unsigned time)
{
    char r[126];
    snprintf(r, 126, "Running deadlock detection at %u\n", time);
    write_log(r);
}

void
write_no_cycles()
{
    char r[126];
    snprintf(r, 126, "No cycles detected\n");
    write_log(r);
}

void
write_cycles(unsigned n)
{
    char r[126];
    snprintf(r, 126, "Detected %u cycles\n", n);
    write_log(r);
}

void
write_cycle()
{
    char r[126];
    snprintf(r, 126, "Detected 1 cycle\n");
    write_log(r);
}

void
write_to_be_killed(cycle c)
{
    char r[126];
    snprintf(r, 126, "Processes marked for termination:\n");
    write_log(r);
    for(unsigned i=0; i<c.size; i++){
        char t[4];
        snprintf(t, 4, "%u\n", c.cycles[i].array[0]);
        write_log(t);
    }
    char s[126];
    snprintf(s, 126, "Marked processes terminated, system no longer in deadlock\n");
    write_log(s);
}

void
write_table(PTR)
{
    VRBS;
    char tmp[MAX_PROCESSES][3];
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        unsigned k = 0;
        for(; (((*u_mems)[i])->resourses)[k] != 1; k++);

        if(((*u_mems)[i])->status == HOLDING) snprintf(tmp[i], 3, "%u", k);
        else snprintf(tmp[i], 3, " ");
    }
    write_log(       "         Current allocations:\n");
    write_log(       "         p1  p2  p3  p4  p5  p6  p7  p8  p9  p10 p11 p12 p13 p14 p15 p16 p17 p18\n");
    char r[126];
    snprintf(r, 126, "resourse %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s %-3s\n",
            tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5], tmp[6], tmp[7], tmp[8], tmp[9], tmp[10], tmp[11], tmp[12], tmp[13], tmp[14], tmp[15], tmp[16], tmp[17]);
    write_log(r);
}

void
write_release(user_mem** process, unsigned resourse, unsigned time)
{
    VRBS;
    char r[126];
    snprintf(r, 126, "Event: process %u released resourse %u; handled at %u\n", (*process)->id, resourse, time);
    write_log(r);
}

void
write_request(user_mem** process, unsigned resourse, unsigned time)
{
    VRBS;
    char r[126];
    snprintf(r, 126, "Event: process %u requested resourse %u; handled at %u\n", (*process)->id, resourse, time);
    write_log(r);
}

void
write_granted(user_mem** process, unsigned resourse, unsigned time)
{
    VRBS;
    char r[126];
    snprintf(r, 126, "Process %u granted resourse %u at %u\n", (*process)->id, resourse, time);
    write_log(r);
}

void
write_denied(user_mem** process, unsigned resourse, unsigned time)
{
    VRBS;
    char r[126];
    snprintf(r, 126, "Process %u denied resourse %u at %u\n", (*process)->id, resourse, time);
    write_log(r);
}

void
write_terminated(user_mem** process, unsigned time)
{
    VRBS;
    char r[126];
    snprintf(r, 126, "Process %u terminated at %u\n", (*process)->id, time);
    write_log(r);
}

void
write_killing(user_mem** process)
{
    VRBS;
    char r[126];
    snprintf(r, 126, "Killing process %u\n", (*process)->id);
    write_log(r);
}

void
write_created(user_mem** process, unsigned time)
{
    VRBS;
    char r[126];
    snprintf(r, 126, "Process %u created at \n", (*process)->id, time);
    write_log(r);
}

void*
create_memory_block(char* fpath, unsigned size)
{
	int memFd = shm_open(fpath, O_RDWR|O_CREAT, 0600);
	check_error(memFd != -1, "shm_open failed");
	check_error(ftruncate(memFd, size) != -1, "ftruncate failed");
	void* addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, memFd, 0);
	check_error(addr != MAP_FAILED, "mmap failed");
	close(memFd);
	return addr;
}

void*
create_user_block(unsigned n, unsigned size)
{
    char tmp[4];
    snprintf(tmp, 4, "/%u", n);
    return create_memory_block(tmp, size);
}

void
init_share_mem(shared_mem** sh_mem)
{
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        ((*sh_mem)->processes)[i] = 0;
        ((*sh_mem)->waiting)[i] = 0;
    }
    for(unsigned i=0; i<MAX_RESOURSES; i++){
        ((*sh_mem)->resourses)[i] = 0;
        if(rand()%100 < 20) ((*sh_mem)->shared_qm)[i] = 1;
        else ((*sh_mem)->shared_qm)[i] = 0;
    }
    (*sh_mem)->seconds = 0;
    (*sh_mem)->nanoseconds = 0;
}

void
init_user_mem(user_mem** u_mem)
{
    sem_init(&((*u_mem)->sem_r), 1, 1);
    sem_init(&((*u_mem)->sem_w), 1, 0);
    (*u_mem)->oss_msg = 0;
    (*u_mem)->usr_msg = 0;
    (*u_mem)->status = 0;
    (*u_mem)->id = next_id++; 
    for(unsigned i = 0; i<MAX_PROCESSES; i++)
        ((*u_mem)->resourses)[i] = 0;
}

void
clean_and_exit(PTR)
{
    munmap((*sh_mem), sizeof(shared_mem));
    shm_unlink("/shared_mem");
    killpg(getpid(), SIGINT);
    while (wait(NULL) > 0);
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        char tmp[4];
        snprintf(tmp, 4, "/%u", i);
        shm_unlink(tmp);
    }
    free(*u_mems);
    exit(EXIT_SUCCESS);
}

void
init(PTR)
{
    *sh_mem = create_memory_block("/shared_mem", sizeof(shared_mem));
    init_share_mem(&(*sh_mem));
    *u_mems = malloc(MAX_PROCESSES * sizeof(user_mem*));
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        (*u_mems)[i] = create_user_block(i, sizeof(user_mem));
    }
}

void
spawn_process(unsigned n)
{
    char tmp[4];
    snprintf(tmp, 4, "/%u", n);
    pid_t pid = fork();
    if(pid == 0) execl("./process", "process", tmp, (char *) NULL);
    else return;
}

void
create_process(PTR)
{
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        if(((*sh_mem)->processes)[i] == 0){
            ((*sh_mem)->processes)[i] = 1;
            init_user_mem(&((*u_mems)[i]));
            return spawn_process(i);
        }
    }
}

void
handle_processes(PTR)
{
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        if(((*sh_mem)->processes)[i] == 0) continue; //no process to handle
        clock_tick(&(*sh_mem), 5);
        sem_wait(&(((*u_mems)[i])->sem_w)); //waitis for process to post message
        unsigned res = ((*u_mems)[i])->request;
        if(((*u_mems)[i])->usr_msg == REQUEST){
            if(((*sh_mem)->waiting)[i] == 0){ //write only first request
                write_request(&((*u_mems)[i]), res, (*sh_mem)->nanoseconds);
                ((*sh_mem)->waiting)[i] = 1;
            }
            if(((*sh_mem)->shared_qm)[res] > 0){ //always grant shared memory
                ((*sh_mem)->shared_qm)[res] += 1;
                ((*sh_mem)->resourses)[res] = 1;
                granted_n++;
                (((*u_mems)[i])->resourses)[res] = 1;
                ((*u_mems)[i])->oss_msg = GRANTED;
                ((*sh_mem)->waiting)[i] = 0;
                write_granted(&((*u_mems)[i]), res, (*sh_mem)->nanoseconds);
                sem_post(&(((*u_mems)[i])->sem_r));
                continue;
            }
            if(((*sh_mem)->resourses)[res] == 0){ //grant if resourse is avaible
                ((*sh_mem)->resourses)[res] = 1;
                granted_n++;
                (((*u_mems)[i])->resourses)[res] = 1;
                ((*u_mems)[i])->oss_msg = GRANTED;
                ((*sh_mem)->waiting)[i] = 0;
                write_granted(&((*u_mems)[i]), res, (*sh_mem)->nanoseconds);
                sem_post(&(((*u_mems)[i])->sem_r));
            }
            else{
                if(((*sh_mem)->waiting)[i] == 0){ // write only first denie 
                    write_denied(&((*u_mems)[i]), res, (*sh_mem)->nanoseconds);
                }
                ((*u_mems)[i])->oss_msg = DENIED;
                sem_post(&(((*u_mems)[i])->sem_r));
            }
        }
        else if(((*u_mems)[i])->usr_msg == RELEASE){
            if((((*u_mems)[i])->resourses)[res] == 1){ //release only if process actually hold that resourse
                (((*u_mems)[i])->resourses)[res] == 0;
                write_release(&((*u_mems)[i]), res, (*sh_mem)->nanoseconds);
                if(((*sh_mem)->shared_qm)[res] > 0){
                    if(((*sh_mem)->shared_qm)[res] > 1)
                        ((*sh_mem)->shared_qm)[res] -= 1;
                    if(((*sh_mem)->shared_qm)[res] == 1)
                        ((*sh_mem)->resourses)[res] = 0;
                }
                else{
                    ((*sh_mem)->resourses)[res] = 0;
                }
            }
            sem_post(&(((*u_mems)[i])->sem_r));
        }
        else if(((*u_mems)[i])->usr_msg == TERMINATED){ //terminate carefuly
            died_of_natural_cause_n++;
            write_terminated(&((*u_mems)[i]), (*sh_mem)->nanoseconds);
            for(unsigned k=0; k<MAX_RESOURSES; k++){ //releases held resourses
                if((((*u_mems)[i])->resourses)[k] == 1){
                    if(((*sh_mem)->shared_qm)[k] > 0){
                        if(((*sh_mem)->shared_qm)[k] > 1)
                            ((*sh_mem)->shared_qm)[k] -= 1;
                        if(((*sh_mem)->shared_qm)[k] == 1)
                            ((*sh_mem)->resourses)[k] = 0;
                    }
                    else{
                        ((*sh_mem)->resourses)[k] = 0;
                    }
                }
            }
            ((*sh_mem)->processes)[i] = 0;
            sem_destroy(&(((*u_mems)[i])->sem_w));
            sem_destroy(&(((*u_mems)[i])->sem_r));
        }
        else{
            sem_post(&(((*u_mems)[i])->sem_r));
        }
    }
}

int
find_connection(unsigned holding[MAX_PROCESSES][MAX_RESOURSES], int process_req[MAX_PROCESSES], unsigned n, unsigned p)
{   //find which process holds given resourse
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        if(i==p) continue; //can't request from itself, but just in case
        if(process_req[i] == -1) continue; //non blocking processes don't matter, but just in case
        if(holding[i][n] == 1) return i; // if process i holds resourse n then return i
    }
    return -1;
}


void
deadlock_resolve(PTR)
{
    dd_run_n++;
    write_deadlock((*sh_mem)->nanoseconds);
    clock_tick(&(*sh_mem), 250);
    unsigned holding[MAX_PROCESSES][MAX_RESOURSES]; //table showing which process holds whihc resourse
    for(unsigned i=0; i<MAX_PROCESSES; i++)
        for(unsigned k=0; k<MAX_RESOURSES; k++)
            holding[i][k] = 0;
    int process_req[MAX_PROCESSES]; //array showing which resourse are processes waiting for
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        if(((*u_mems)[i])->usr_msg == REQUEST){
            unsigned res = ((*u_mems)[i])->request;
            if(((*sh_mem)->shared_qm)[res] == 0){ //if process is waiting for share memory then it will be given by default in next cycle
                process_req[i] = res;
                for(unsigned k=0; k<MAX_RESOURSES; k++){
                    if((((*u_mems)[i])->resourses)[k] == 1){
                        if(((*sh_mem)->shared_qm)[k] == 0){ //note only non-shared resourses
                            holding[i][k] = 1;
                        }
                    }
                }
            }
            else
                process_req[i] = -1;
        }
        else{ //if process is not waiting blocked then it doesn't cause deadlock
            process_req[i] = -1;
        }
    }
    cycle c = cycle_new();
    for(unsigned i=0; i<MAX_PROCESSES; i++){ //find cycles
        if(process_req[i] == -1) continue; //process doesn't contribute to deadlock
        if(cycle_find(c, i) != -1) continue; //if process is already part of the cycle continue with next process
        vector next_c = vector_new();
        unsigned p = i;
        vector_add(&next_c, p); //curent vector
        int next_p;
        while(1){
            next_p = find_connection(holding, process_req, process_req[p], p);
            if(next_p == -1) break; //chain of requests end
            p = next_p;
            vector_add(&next_c, p);
            if(vector_find(next_c, p) != -1) break; //cycle formed
        }
        if(next_c.array[0] == next_c.array[next_c.size -1]){ //cound cycle only if it ends where it begins
            if(next_c.size > 1)
                cycle_add(&c, next_c);
        }
    }
    cycle_show(c);
    if(c.size == 0) {
        write_no_cycles();
        return;
    }
    if(c.size == 1) {
        write_cycle();
    }
    else{
        write_cycles(c.size);
    }
    write_to_be_killed(c);
    for(unsigned i=0; i<c.size; i++){
        terminated_by_deadlock_n++;
        ((*u_mems)[c.cycles[i].array[0]])->oss_msg = TERMINATE; //remove one node from a cycle and it colapses
        sem_post(&(((*u_mems)[c.cycles[i].array[0]])->sem_r));
    }
}


int
main()
{
    srand(time(NULL));
    system("rm -f log.log");
    shared_mem* sh_mem; user_mem** u_mems;
    init(ADR);

    unsigned last_deadlock_check = 0;
    unsigned last_process_spawn = 0;
    create_process(ADR);
    while(1){
        if(sh_mem->nanoseconds > 20000){
            write_exit();
            clean_and_exit(ADR);
        }
        if((sh_mem->nanoseconds - last_process_spawn) > (rand() % MAX_WAIT_BETWEEN_PROCESSES_SPAWN)){
            last_process_spawn=sh_mem->nanoseconds;
            create_process(ADR);
        }
        handle_processes(ADR);
        if((sh_mem->nanoseconds - last_deadlock_check) > DEADLOCK_CHECK){
            last_deadlock_check = sh_mem->nanoseconds;
            deadlock_resolve(ADR);
        }
    }

    return 0;
}
