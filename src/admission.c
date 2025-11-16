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
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

#define MESSAGE_QUEUE_KEY 1234
#define MAX_NAME_LEN 50
#define MAX_PRIORITY 5
#define INPUT_PIPE "/tmp/input_pipe"
#define BUF_SIZE 1024
#define LOG_FILE "DEI_Emergency.log"
#define LOG_MMAP_SIZE (10 * 1024 * 1024)  // 10 MB

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

int input_fd = -1;                // ficheiro de input
settings global_settings;              

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

/* MMF globals */
static int log_fd = -1;
static void *log_map = NULL;       // pointer para o mmf
static size_t log_map_size = LOG_MMAP_SIZE;
static const char *LOG_SEM_NAME = "/dei_log_sem";
static sem_t *log_sem = NULL;      // agora usa sem_open

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
    if (global_settings.max_queue_size <= 0) {
        fprintf(stderr, "[ERROR_CONFIG]: {MAX_QUEUE_SIZE} must be >= 1\n");
            return 1;
    }
    if (global_settings.num_screening_threads <= 0) {
        fprintf(stderr, "[ERROR_CONFIG]: {NUM_SCREENING_THREADS} must be >= 1\n");
        return 1;
    }
    if (global_settings.num_doctor_processes <= 0) {
        fprintf(stderr, "[ERROR_CONFIG]: {NUM_DOCTOR_PROCESSES} must be >= 1\n");
        return 1;
    }
    if (global_settings.shift_length < 0) {
        fprintf(stderr, "[ERROR_CONFIG]: {SHIFT_LENGTH} must be >= 0\n");
        return 1;
    }
    if (global_settings.msg_wait_max <= 0) {
        fprintf(stderr, "[ERROR_CONFIG]: {MSG_WAIT_MAX} must be >= 1\n");
        return 1;
    }
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

int init_log_mmf() {
    // abre/cria ficheiro
    log_fd = open(LOG_FILE, O_RDWR | O_CREAT, 0600);
    if (log_fd < 0) {
        perror("open log file");
        return 1;
    }

    // garante tamanho
    if (ftruncate(log_fd, log_map_size) < 0) {
        perror("ftruncate log file");
        close(log_fd);
        return 1;
    }

    // mapa em memória partilhada (MAP_SHARED para partilhar com forks)
    log_map = mmap(NULL, log_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, log_fd, 0);
    if (log_map == MAP_FAILED) {
        perror("mmap log file");
        close(log_fd);
        log_map = NULL;
        return 1;
    }

    // criar/open do semaphore nomeado (pshared across processes)
    // remove sem antigo se existir (não faz mal se não existir)
    sem_unlink(LOG_SEM_NAME);
    log_sem = sem_open(LOG_SEM_NAME, O_CREAT | O_EXCL, 0600, 1);
    if (log_sem == SEM_FAILED) {
        // se já existir, tenta abrir
        log_sem = sem_open(LOG_SEM_NAME, 0);
        if (log_sem == SEM_FAILED) {
            perror("sem_open");
            munmap(log_map, log_map_size);
            close(log_fd);
            log_map = NULL;
            return 1;
        }
    }

    // inicializa offset a 0 se estiver a começar (só a primeira criação deve escrever)
    // sincroniza para garantir que apenas o creator inicializa
    sem_wait(log_sem);
    uint64_t *offptr = (uint64_t *)log_map;
    if (*offptr == 0) {
        // se for zero, assume-se vazio -> escreve cabeçalho inicial (pode estar a zeros)
        *offptr = 0;
    }
    sem_post(log_sem);

    return 0;
}

int append_log_mmf(const char *fmt, ...) {
    if (!log_map || !log_sem) return 1;

    // preparar mensagem com timestamp
    char msg[1024];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

    va_list ap;
    va_start(ap, fmt);
    char body[900];
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    snprintf(msg, sizeof(msg), "%s %s\n", ts, body);
    size_t msglen = strlen(msg);

    // sincroniza entre processos
    sem_wait(log_sem);

    uint64_t *offptr = (uint64_t *)log_map;
    uint64_t offset = *offptr; // offset relativo a log_map + 8

    // localização de escrita
    size_t header = sizeof(uint64_t);
    if (header + offset + msglen >= log_map_size) {
        // sem espaço suficiente: comportamento possível -> truncar, ou rolar, ou rejeitar.
        // Aqui: escrevemos uma mensagem de aviso no próprio log e fechamos (evita overflow).
        const char *err = "LOG MMF FULL: cannot append message\n";
        size_t errlen = strlen(err);
        if (header + offset + errlen < log_map_size) {
            memcpy((char *)log_map + header + offset, err, errlen);
            *offptr = offset + errlen;
            msync(log_map, log_map_size, MS_SYNC);
        }
        sem_post(log_sem);
        return 1;
    }

    memcpy((char *)log_map + header + offset, msg, msglen);
    offset += msglen;
    *offptr = offset;

    // força escrita no disco (durability). Podes omitir msync por performance, caso aceites risco.
    msync(log_map, log_map_size, MS_SYNC);

    sem_post(log_sem);

    // também imprime no stdout, como exige o enunciado
    printf("%s", msg);

    return 0;
}

int write_to_log(char *log_info) {
    append_log_mmf("%s", log_info);
    return 0;   
}

void close_log_mmf() {
    if (log_map) {
        msync(log_map, log_map_size, MS_SYNC);
        munmap(log_map, log_map_size);
        log_map = NULL;
    }
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
    if (log_sem) {
        sem_close(log_sem);
        sem_unlink(LOG_SEM_NAME);
        log_sem = NULL;
    }
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

int create_and_open_input_pipe() {
    // cria o named pipe se não existir
    if ((mkfifo(INPUT_PIPE, 0600) < 0) && (errno != EEXIST)) {
        write_to_log("[ERROR] Could not create input_pipe");
        return 1;
    }

    // abre o pipe em modo leitura/escrita
    // O_RDWR evita que open() bloqueie quando ainda não há writers
    input_fd = open(INPUT_PIPE, O_RDWR);
    if (input_fd < 0) {
        write_to_log("[ERROR] Could not open input_pipe");
        return 1;
    }

    write_to_log("input_pipe created and opened successfully");
    return 0;
}

int read_line_from_pipe(char *buffer, size_t size) {
    ssize_t n = read(input_fd, buffer, size - 1);

    if (n <= 0) {
        return 0; // pipe vazio ou EOF
    }

    buffer[n] = '\0';

    // remover newline se existir
    char *nl = strchr(buffer, '\n');
    if (nl) *nl = '\0';

    return 1;
}

int line_is_triage_command(char *line) {
    return strncmp(line, "TRIAGE=", 7) == 0;
}

int line_is_group(char *line) {
    // grupo começa com número inteiro
    return isdigit(line[0]);
}

int parse_single_patient(char *line, admission_data *p) {
    char name[MAX_NAME_LEN];
    int stime, atime, prio;

    if (sscanf(line, "%s %d %d %d", name, &stime, &atime, &prio) != 4) {
        write_to_log("[ERROR] Invalid patient format");
        return 0;
    }

    static int next_id = 1;

    p->patient_id = next_id++;
    strncpy(p->name, name, MAX_NAME_LEN);
    p->screening_time = stime;
    p->service_time = atime;
    p->priority = prio;

    return 1;
}

void apply_triage_change(char *line) {
    int new_value = atoi(line + 7);

    if (new_value <= 0) {
        write_to_log("[ERROR] Invalid TRIAGE value");
        return;
    }

    char buf[128];
    sprintf(buf, "Received TRIAGE=%d command", new_value);
    write_to_log(buf);

    // FUTURO: aqui iremos recriar threads (podes deixar assim por agora)
    global_settings.num_screening_threads = new_value;
}

int parse_group(char *line, int *count, int *stime, int *atime, int *prio) {
    if (sscanf(line, "%d %d %d %d", count, stime, atime, prio) != 4) {
        return 0;
    }
    return 1;
}

void generate_auto_name(char *dest, int index) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    sprintf(dest, "%04d-%02d-%02d-%03d",
        tm->tm_year + 1900,
        tm->tm_mon + 1,
        tm->tm_mday,
        index);
}

void create_group_patients(int count, int stime, int atime, int prio) {
    for (int i = 1; i <= count; i++) {
        admission_data p;

        static int next_id = 1;
        p.patient_id = next_id++;

        generate_auto_name(p.name, i);
        p.screening_time = stime;
        p.service_time = atime;
        p.priority = prio;

        if (triage_push(&triage_q, &p) != 0) {
            write_to_log("[ERROR] triage queue full — discarding grouped patient");
        }
    }
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

    if (input_fd >= 0) {
        close(input_fd);
    }
    unlink(INPUT_PIPE);


    write_to_log("Cleanup complete.");
    close_log_mmf();
}

/* ========================= MAIN ========================= */

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    log_sem = malloc(sizeof(sem_t));
    sem_init(log_sem, 1, 1);

    if (init_log_mmf() != 0) {
        fprintf(stderr, "Failed to initialize log mmf\n");
        return 1;
    }

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

    if (create_and_open_input_pipe() != 0) {
        printf("Failed to initialize input pipe\n");
        return 1;
    }

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

    char buf[BUF_SIZE];
    
    while (running) {
        if (!read_line_from_pipe(buf, sizeof(buf)))
        continue;

        if (line_is_triage_command(buf)) {
            apply_triage_change(buf);
            continue;
        }

        if (line_is_group(buf)) {
            int count, stime, atime, prio;
            if (parse_group(buf, &count, &stime, &atime, &prio)) {
                create_group_patients(count, stime, atime, prio);
                continue;
            }
        }

        // caso contrário → paciente normal
        admission_data p;

        if (!parse_single_patient(buf, &p))
            continue;

        triage_push(&triage_q, &p);

        char logbuf[128];
        sprintf(logbuf, "Received patient %s from pipe", p.name);
        write_to_log(logbuf);
    }

    /* termina */
    for (int i = 0; i < global_settings.num_screening_threads; i++)
        pthread_join(triage_threads[i], NULL);

    cleanup(pids);

    return 0;
}


