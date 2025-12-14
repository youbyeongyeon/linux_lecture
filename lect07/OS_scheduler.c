#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#define NUM_PROCESSES 10 // 총 프로세스 수
#define TIME_QUANTUM 5   // 라운드 로빈 시간 할당량
#define MAX_CPU_BURST 10 // 최대 CPU 버스트 시간 (1~10)
#define MAX_IO_WAIT 5    // 최대 I/O 대기 시간 (1~5)

typedef enum { READY, RUNNING, SLEEP, DONE } ProcessStatus; 

// PCB 구조체 정의
typedef struct {
    pid_t pid;
    int index;
    int time_quantum;    
    int cpu_burst;       // 남은 CPU 버스트 (실행 중 감소)
    int initial_burst;   // 초기 버스트 값 (결과 출력용)
    int io_wait_time;    
    ProcessStatus status; 
    long total_wait_time; 
} PCB;

PCB pcb_table[NUM_PROCESSES];
int current_running_index = -1; 
int total_running_time = 0;     

void parent_scheduler(int signo);
void child_handler(int signo);
void initialize_processes();
void schedule_next_process();
void io_request_handler(int signo);
void child_termination_handler(int signo); 
void terminate_simulation();
int find_pcb_index(pid_t pid);


// =================================================================
// 1. 메인 함수: 부모 프로세스 (커널 역할)
// =================================================================
int main() {
    srand(time(NULL));
    initialize_processes(); 

    printf("--- OS 라운드 로빈 스케줄러 시뮬레이션 시작 ---\n");
    printf("총 프로세스: %d, 시간 할당량(Time Quantum): %d\n", NUM_PROCESSES, TIME_QUANTUM);

    // 시그널 핸들러 설정
    if (signal(SIGALRM, parent_scheduler) == SIG_ERR) { perror("SIGALRM 시그널 오류"); exit(1); }
    if (signal(SIGUSR1, io_request_handler) == SIG_ERR) { perror("SIGUSR1 시그널 오류"); exit(1); }
    if (signal(SIGCHLD, child_termination_handler) == SIG_ERR) { perror("SIGCHLD 시그널 오류"); exit(1); }

    schedule_next_process(); // 최초 스케줄링
    alarm(1); // 1초마다 SIGALRM 발생

    while (1) {
        pause(); // 시그널을 기다리며 대기
    }

    return 0;
}

// PID를 통해 PCB 테이블 인덱스 찾기
int find_pcb_index(pid_t pid) {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb_table[i].pid == pid) return i;
    }
    return -1;
}

// 10개의 자식 프로세스를 fork()로 생성하고 PCB를 초기화
void initialize_processes() {
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pcb_table[i].pid = fork(); 

        if (pcb_table[i].pid == 0) {
            child_handler(0); 
            exit(0); 
        } else if (pcb_table[i].pid > 0) {
            // PCB 초기화
            int burst_val = rand() % MAX_CPU_BURST + 1; // 랜덤 값 생성

            pcb_table[i].index = i;
            pcb_table[i].cpu_burst = burst_val;         // 남은 버스트 (실행 중 감소)
            pcb_table[i].initial_burst = burst_val;     // 초기 버스트 값 저장
            pcb_table[i].time_quantum = TIME_QUANTUM;
            pcb_table[i].status = READY; 
            pcb_table[i].io_wait_time = 0;
            pcb_table[i].total_wait_time = 0;
            printf("[커널] 프로세스 P%d (PID %d) 생성. 초기 버스트: %d\n", 
                   i, pcb_table[i].pid, pcb_table[i].cpu_burst);

            // 자식에게 SIGSTOP을 보내 Ready 상태에서 대기
            kill(pcb_table[i].pid, SIGSTOP); // 프로세스 중지
        } else {
            perror("fork 오류"); exit(1);
        }
    }
}

// READY 상태의 프로세스를 찾아 스케줄링
void schedule_next_process() {
    int next_index = -1;
    // 라운드 로빈
    int start_index = (current_running_index + 1) % NUM_PROCESSES; 

    for (int i = 0; i < NUM_PROCESSES; i++) {
        int idx = (start_index + i) % NUM_PROCESSES;
        if (pcb_table[idx].status == READY) {
            next_index = idx;
            break;
        }
    }

    if (next_index != -1) {
        // 새로운 프로세스 실행
        current_running_index = next_index;
        pcb_table[next_index].status = RUNNING; // 실행 상태로 변경
        pcb_table[next_index].time_quantum = TIME_QUANTUM; // Time Quantum 재설정

        // 프로세스 재개 (SIGCONT)
        kill(pcb_table[next_index].pid, SIGCONT);
        printf("\n[커널: 틱 %d] 스케줄링 P%d (PID %d) 시작. Time Q: %d, 남은 Burst: %d\n", 
               total_running_time, next_index, pcb_table[next_index].pid, TIME_QUANTUM, pcb_table[next_index].cpu_burst);
    } else {
        // 실행할 프로세스가 없음 (모두 SLEEP 또는 DONE)
        current_running_index = -1;
    }
}

// SIGALRM 핸들러: 타이머 인터럽트 처리 (커널 역할)
void parent_scheduler(int signo) {
    total_running_time++;

    // 1. I/O 대기 큐 처리 (SLEEP -> READY)
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb_table[i].status == SLEEP) { // 대기 상태인 프로세스 처리
            pcb_table[i].io_wait_time--;
            if (pcb_table[i].io_wait_time == 0) {
                pcb_table[i].status = READY;
                printf("[커널: 틱 %d] P%d I/O 완료. -> READY\n", 
                       total_running_time, i);
            }
        }
    }

    // 2. Ready 큐 대기 시간 누적
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb_table[i].status == READY) {
            pcb_table[i].total_wait_time++; 
        }
    }

    // 3. RUNNING 프로세스 처리
    if (current_running_index != -1) {
        PCB *current = &pcb_table[current_running_index];
        current->time_quantum--;
        
        // 커널이 PCB 버스트를 1 감소시키고 자식에게 실행 명령을 내림 (강제 동기화)
        current->cpu_burst--; 
        kill(current->pid, SIGUSR2); // 자식에게 CPU 틱 소모 명령 전송

        // 종료 체크: PCB 버스트가 0 이하가 되면 즉시 종료 처리
        if (current->cpu_burst <= 0) {
            printf("[커널: 틱 %d] P%d CPU 버스트 완료 (PCB 기준). -> 종료\n", total_running_time, current_running_index);
            kill(current->pid, SIGKILL); // 자식 프로세스를 강제 종료 (SIGCHLD 발생 유도)
            
            current_running_index = -1;
            schedule_next_process();
            alarm(1); 
            return;
        }

        // Time Quantum 만료 판별
        if (current->time_quantum == 0) {
            printf("[커널: 틱 %d] P%d Time Quantum 만료. -> READY\n", 
                   total_running_time, current_running_index);

            current->status = READY;
            kill(current->pid, SIGSTOP); // 프로세스 정지
            
            schedule_next_process(); // 다음 프로세스 스케줄링
        }
    } else {
        schedule_next_process();
    }

    alarm(1); // 타이머 재설정
}

// SIGUSR1 핸들러: 자식의 I/O 요청 처리 (커널 역할)
void io_request_handler(int signo) {
    if (current_running_index != -1) {
        PCB *current = &pcb_table[current_running_index];

        // I/O 대기 시간 할당 (1~5 랜덤)
        current->io_wait_time = rand() % MAX_IO_WAIT + 1; 
        current->status = SLEEP; // 대기 상태로 변경 
        printf("[커널: 틱 %d] P%d (PID %d) I/O 요청. -> SLEEP (대기: %d)\n", 
               total_running_time, current_running_index, current->pid, current->io_wait_time);

        kill(current->pid, SIGSTOP); // 프로세스 정지

        schedule_next_process(); // 다음 프로세스 스케줄링
    }
}

// SIGCHLD 핸들러: 자식 프로세스 종료 시 호출
void child_termination_handler(int signo) {
    pid_t pid;
    
    // 종료된 자식 프로세스들을 Non-blocking 모드로 모두 회수 (좀비 방지)
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) { 
        int index = find_pcb_index(pid);
        if (index != -1) {
            pcb_table[index].status = DONE;
            
            // 현재 실행 중이던 프로세스가 종료되었으면 스케줄러 호출
            if (index == current_running_index) {
                current_running_index = -1;
                schedule_next_process();
            }
            printf("[커널: 틱 %d] P%d (PID %d) 종료됨. PCB DONE.\n", total_running_time, index, pid);
        }
    }

    // 모든 프로세스가 DONE인지 확인하고 종료
    int done_count = 0;
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb_table[i].status == DONE) done_count++;
    }

    if (done_count == NUM_PROCESSES) {
        terminate_simulation();
    }
}

// 시뮬레이션 종료 및 결과 출력 (성능 분석 포함)
void terminate_simulation() {
    double total_wait = 0;
    int total_finished = 0;

    // SIGALRM 비활성화
    alarm(0); // 알람 취소

    printf("\n=================================================\n");
    printf("        OS 라운드 로빈 스케줄러 시뮬레이션 종료      \n");
    printf("=================================================\n");
    printf("총 시뮬레이션 시간: %d 틱\n", total_running_time);

    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (pcb_table[i].pid > 0) {
            printf("[결과 P%d] PID: %d, 초기 Burst: %d, 총 대기 시간(Ready): %ld\n", 
                   i, pcb_table[i].pid, pcb_table[i].initial_burst, pcb_table[i].total_wait_time);
            total_wait += pcb_table[i].total_wait_time;
            total_finished++;
        }
    }

    if (total_finished > 0) {
        printf("\n==> 프로세스 평균 대기 시간: %.2f 틱\n", total_wait / total_finished);
    }
    
    exit(0);
}


// =================================================================
// 2. 자식 프로세스 핸들러: 자식 프로세스 (응용 역할)
// =================================================================

// 자식 프로세스는 자신의 실제 CPU 버스트만 관리
static int my_cpu_burst_data = 0; 
static int is_initialized = 0; 

// SIGCONT (재개) 또는 SIGUSR2 (CPU 틱 명령)을 처리하는 핸들러
void child_handler(int signo) {
    pid_t my_pid = getpid(); 

    // 1. 최초 초기화 (fork 후 단 한 번만 실행)
    if (is_initialized == 0) {
        // 시그널 핸들러 등록
        if (signal(SIGCONT, child_handler) == SIG_ERR || signal(SIGUSR2, child_handler) == SIG_ERR) { 
            perror("자식 시그널 핸들러 등록 오류"); exit(1);
        }
        
        srand(my_pid); 
        // 자식의 버스트 값은 출력 용도 및 I/O 체크 용도로만 사용됨
        my_cpu_burst_data = rand() % MAX_CPU_BURST + 1; 
        printf("[자식 %d] 초기 버스트: %d. 대기 중.\n", my_pid, my_cpu_burst_data);
        is_initialized = 1; // 초기화 완료 플래그 설정
    }
    
    // 2. SIGUSR2 처리 (CPU 틱 소모)
    if (signo == SIGUSR2) {
        
        if (my_cpu_burst_data > 0) {
            my_cpu_burst_data--; // CPU 버스트 1 감소 (자식 측 로그용)

            // 1. I/O 요청 판별 (20% 확률)
            if (my_cpu_burst_data > 0 && (rand() % 10 < 2)) {
                printf("[자식 %d] CPU: %d 남음 -> I/O 요청 (SIGUSR1)\n", my_pid, my_cpu_burst_data + 1);
                kill(getppid(), SIGUSR1); // 부모에게 I/O 요청 전송
            } 
            // 2. 프로세스 종료 판별 (버스트가 0이 되었을 때)
            // 자식은 SIGKILL을 기다림 (커널이 SIGKILL을 보냄)
            else if (my_cpu_burst_data <= 0) {
                printf("[자식 %d] 버스트 0 도달. 종료 대기 중...\n", my_pid);
            }
            // 3. 계속 실행
            else {
                printf("[자식 %d] 실행 중... 남은 CPU: %d\n", my_pid, my_cpu_burst_data);
            }
        }
    }
    
    // 3. 메인 대기 루프
    while(1) {
        pause(); 
    }
}
