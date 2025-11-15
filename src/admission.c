/*
 * admission.c — versão final, compilável, integrada
 * Para a defesa intermédia de SO (2025–2026)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>

#define LOG_FILE "log.txt"
#define MESSAGE_QUEUE_KEY 1234
#define MAX_NAME_LEN 50
#define MAX_PRIORITY 5

/* ======================== STRUCTS ======================== */
typedef struct settings {
    int max_queue_size;
    int num_screening_threads;
    int num_doctor_processes;
    int shift_length;
    int msg_wait_max;
} settings;

typedef struct admission_data {
    int patient_id;
    char name[MAX_NAME_LEN];
    int screening_time;
    int service_time;
    int priority;
} admission_data;

typedef struct msgbuf {
    long mtype;
    admission_data payload;
} msgbuf_t;

/* ======================== GLOBALS EXISTENTES ======================== */

settings global_settings;              // <-- ESTA é a versão correta (única)

sem_t *log_sem;                 // semáforo para o log
FILE *logfd;                    // ficheiro de log

int message_queue_id;           // message queue

int running = 1;                // flag de execução

/* SHM (placeholder da tua versão original) */
sem_t admission_sem;
int admission_shm_id;
admission_data *admission_array;

sem_t doctor_sem;
int doctor_shm_id;
int *doctor_array;

/* ======================= TRIAGE QUEUE ======================== */

typedef struct triage_queue {
    admission_data *buf;
    int capacity;
    int head, tail, size;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} triage_queue_t;

triage_queue_t triage_q;
pthread_t *triage_threads;

/* ============================================================= */
/* ==================== FUNÇÕES EXISTENTES ===================== */
/* ============================================================= */

int write_to_log(char *log_info) {
    sem_wait(log_sem);
    logfd = fopen(LOG_FILE, "a");
    if (!logfd) {
        sem_post(log_sem);
        return 1;
    }

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[80];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(logfd, "%s %s\n", ts, log_info);
    printf("%s %s\n", ts, log_info);

    fclose(logfd);
    sem_post(log_sem);
    return 0;
}

int read_configfile(char *config_file) {
    FILE *f = fopen(config_file, "r");
    if (!f) return 1;

    if (fscanf(f, "%d\n%d\n%d\n%d\n%d",
               &global_settings.max_queue_size,
               &global_settings.num_screening_threads,
               &global_settings.num_doctor_processes,
               &global_settings.shift_length,
               &global_settings.msg_wait_max) != 5) {
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

int validate_settings() {
    if (global_settings.max_queue_size <= 0) return 1;
    if (global_settings.num_screening_threads <= 0) return 1;
    if (global_settings.num_doctor_processes <= 0) return 1;
    if (global_settings.shift_length < 0) return 1;
    if (global_settings.msg_wait_max <= 0) return 1;
    return 0;
}

int create_admission_shm() {
    admission_shm_id = shmget(IPC_PRIVATE,
                              sizeof(admission_data) * global_settings.num_screening_threads,
                              IPC_CREAT | 0777);
    if (admission_shm_id < 0) return 1;

    admission_array = shmat(admission_shm_id, NULL, 0);

    sem_init(&admission_sem, 1, 1);

    sem_wait(&admission_sem);
    for (int i = 0; i < global_settings.num_screening_threads; i++)
        admission_array[i].patient_id = -1;
    sem_post(&admission_sem);

    return 0;
}

int create_doctor_shm() {
    doctor_shm_id = shmget(IPC_PRIVATE,
                           sizeof(int) * global_settings.num_doctor_processes,
                           IPC_CREAT | 0777);
    if (doctor_shm_id < 0) return 1;

    doctor_array = shmat(doctor_shm_id, NULL, 0);

    sem_init(&doctor_sem, 1, 1);

    sem_wait(&doctor_sem);
    for (int i = 0; i < global_settings.num_doctor_processes; i++)
        doctor_array[i] = 0;
    sem_post(&doctor_sem);

    return 0;
}

/* ============================================================= */
/* ============== TRIAGE QUEUE (threads) ======================== */
/* ============================================================= */

int triage_init(triage_queue_t *q, int capacity) {
    q->buf = calloc(capacity, sizeof(admission_data));
    q->capacity = capacity;
    q->size = 0;
    q->head = q->tail = 0;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    return 0;
}

int triage_push(triage_queue_t *q, admission_data *p) {
    pthread_mutex_lock(&q->mutex);

    while (q->size == q->capacity && running)
        pthread_cond_wait(&q->not_full, &q->mutex);

    if (!running) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    q->buf[q->tail] = *p;
    q->tail = (q->tail + 1) % q->capacity;
    q->size++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int triage_pop(triage_queue_t *q, admission_data *out) {
    pthread_mutex_lock(&q->mutex);

    while (q->size == 0 && running)
        pthread_cond_wait(&q->not_empty, &q->mutex);

    if (!running && q->size == 0) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

/* ===================== TRIAGE THREAD ===================== */
void *triage_thread(void *arg) {
    long id = (long)arg;
    char buf[128];

    sprintf(buf, "Triage thread %ld started", id);
    write_to_log(buf);

    while (running) {
        admission_data p;
        if (triage_pop(&triage_q, &p) != 0)
            break;

        sprintf(buf, "Triage %ld: triaging %s", id, p.name);
        write_to_log(buf);

        struct timespec ts;
        ts.tv_sec = p.screening_time / 1000;
        ts.tv_nsec = (p.screening_time % 1000) * 1000000L;
        nanosleep(&ts, NULL);

        msgbuf_t msg;
        msg.mtype = p.priority;
        msg.payload = p;

        msgsnd(message_queue_id, &msg, sizeof(admission_data), 0);
    }

    sprintf(buf, "Triage thread %ld stopping", id);
    write_to_log(buf);
    return NULL;
}

/* ===================== DOCTOR PROCESS ===================== */

void doctor_process(int index) {
    char buf[128];

    sprintf(buf, "Doctor %d started (pid=%d)", index, getpid());
    write_to_log(buf);

    time_t start = time(NULL);

    while (1) {
        msgbuf_t msg;
        int found = 0;

        /* busca por prioridade */
        for (int p = 1; p <= MAX_PRIORITY; p++) {
            if (msgrcv(message_queue_id, &msg,
                       sizeof(admission_data), p, IPC_NOWAIT) >= 0) {
                found = 1;
                break;
            }
        }

        /* Nada? bloqueia. */
        if (!found) {
            if (msgrcv(message_queue_id, &msg, sizeof(admission_data), 0, 0) < 0) {
                if (errno == EINTR) continue;
                continue;
            }
        }

        admission_data *pt = &msg.payload;

        sprintf(buf, "Doctor %d: attending %s", index, pt->name);
        write_to_log(buf);

        struct timespec ts;
        ts.tv_sec = pt->service_time / 1000;
        ts.tv_nsec = (pt->service_time % 1000) * 1000000L;
        nanosleep(&ts, NULL);

        sprintf(buf, "Doctor %d: finished %s", index, pt->name);
        write_to_log(buf);

        /* shift completo? */
        if (time(NULL) - start >= global_settings.shift_length)
            break;
    }

    sprintf(buf, "Doctor %d exiting", index);
    write_to_log(buf);
    exit(0);
}

/* ========================= SIGNALS ========================= */

void sigint_handler(int s) {
    running = 0;
    pthread_cond_broadcast(&triage_q.not_empty);
    pthread_cond_broadcast(&triage_q.not_full);
    write_to_log("SIGINT received, shutting down.");
}

void sigusr1_handler(int s) {
    write_to_log("SIGUSR1 received — statistics placeholder.");
}

/* ========================= CLEANUP ========================= */

void cleanup(pid_t *pids) {
    for (int i = 0; i < global_settings.num_doctor_processes; i++) {
        kill(pids[i], SIGINT);
        waitpid(pids[i], NULL, 0);
    }

    msgctl(message_queue_id, IPC_RMID, NULL);

    triage_destroy:
    free(triage_q.buf);

    shmdt(admission_array);
    shmctl(admission_shm_id, IPC_RMID, NULL);

    shmdt(doctor_array);
    shmctl(doctor_shm_id, IPC_RMID, NULL);

    write_to_log("Cleanup complete.");
}

/* ========================= MAIN ========================= */

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    log_sem = malloc(sizeof(sem_t));
    sem_init(log_sem, 1, 1);

    if (read_configfile(argv[1]) != 0) {
        printf("Config read failed\n");
        return 1;
    }

    if (validate_settings()) {
        printf("Invalid config\n");
        return 1;
    }

    create_admission_shm();
    create_doctor_shm();

    /* sinais */
    signal(SIGINT, sigint_handler);
    signal(SIGUSR1, sigusr1_handler);

    /* message queue */
    message_queue_id = msgget(MESSAGE_QUEUE_KEY, IPC_CREAT | 0666);
    if (message_queue_id < 0) {
        perror("msgget");
        return 1;
    }

    /* triage queue */
    triage_init(&triage_q, global_settings.max_queue_size);

    /* threads */
    triage_threads = calloc(global_settings.num_screening_threads, sizeof(pthread_t));

    for (long i = 0; i < global_settings.num_screening_threads; i++)
        pthread_create(&triage_threads[i], NULL, triage_thread, (void*)i);

    /* processos doctor */
    pid_t *pids = calloc(global_settings.num_doctor_processes, sizeof(pid_t));

    for (int i = 0; i < global_settings.num_doctor_processes; i++) {
        pid_t pid = fork();
        if (pid == 0)
            doctor_process(i);
        pids[i] = pid;
    }

    write_to_log("Admission ready.");

    /* INJETAR PACIENTES DE TESTE PARA DEMONSTRAÇÃO */
    for (int i = 0; i < global_settings.max_queue_size; i++) {
        admission_data p = {
            .patient_id = i+1,
            .priority = (i % MAX_PRIORITY) + 1,
            .screening_time = 200,
            .service_time = 300
        };
        sprintf(p.name, "TEST-%d", i+1);
        triage_push(&triage_q, &p);
    }

    /* aguarda CTRL+C */
    while (running)
        pause();

    /* termina */
    for (int i = 0; i < global_settings.num_screening_threads; i++)
        pthread_join(triage_threads[i], NULL);

    cleanup(pids);

    return 0;
}

