/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#define _POSIX_C_SOURCE 200809L // Para sigaction e outras funcionalidades POSIX    

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include "doctor.h"
#include "config.h"
#include "shm.h"
#include "msq.h"
#include "log.h"

#define DEBUG 

/* Array global para guardar informação dos doctors */
DoctorInfo *doctors_array = NULL;

/* Contadores para doctors temporários */
int num_temporary_doctors = 0;
int temporary_doctor_counter = 0;

/* Flag para controlar a execução do turno */
volatile sig_atomic_t shift_active = 1;

/* Handler para SIGALRM (fim do turno) */
void sigalrm_handler(int signum) {
    (void)signum; 
    shift_active = 0;
}

/*
 * Bloqueia sinais indesejados nos processos Doctor
 */
void block_unwanted_signals_doctor() {
    sigset_t block_set;
    
    sigemptyset(&block_set);
    
    // Bloquear sinais que não devemos tratar
    sigaddset(&block_set, SIGUSR1);   // Só o Admission trata SIGUSR1
    sigaddset(&block_set, SIGHUP);    // Ignorar desconexão
    sigaddset(&block_set, SIGQUIT);   // Ignorar SIGQUIT (Ctrl+\)
    sigaddset(&block_set, SIGTSTP);   // Ignorar Ctrl+Z
    sigaddset(&block_set, SIGPIPE);   // Ignorar pipe quebrado
    
    if (sigprocmask(SIG_BLOCK, &block_set, NULL) != 0) {
        perror("Erro ao bloquear sinais no Doctor");
    }
}

/*
 * Função principal executada por cada processo Doctor permanente
 */
void doctor_main(int doctor_id, const Config *config) {
    write_log("Doctor %d: Processo iniciado (PID: %d)", doctor_id, getpid());
    
    // Bloquear sinais indesejados
    block_unwanted_signals_doctor();
    
    // Anexar à memória partilhada
    if (attach_shared_memory() != 0) {
        write_log("ERRO: Doctor %d falhou ao anexar à memória partilhada", doctor_id);
        exit(EXIT_FAILURE);
    }
    
    // Obter acesso à fila de mensagens existente
    key_t key = ftok(MSQ_KEY_PATH, MSQ_KEY_ID);
    if (key == -1) {
        write_log("ERRO: Doctor %d falhou ao gerar chave para fila de mensagens", doctor_id);
        detach_shared_memory();
        exit(EXIT_FAILURE);
    }
    
    msq_id = msgget(key, 0666);
    if (msq_id == -1) {
        write_log("ERRO: Doctor %d falhou ao aceder à fila de mensagens", doctor_id);
        detach_shared_memory();
        exit(EXIT_FAILURE);
    }
    
    // Configurar handler para SIGALRM (fim de turno)
    struct sigaction sa_alarm;
    sa_alarm.sa_handler = sigalrm_handler;
    sigemptyset(&sa_alarm.sa_mask);
    sa_alarm.sa_flags = 0;
    
    if (sigaction(SIGALRM, &sa_alarm, NULL) == -1) {
        write_log("ERRO: Doctor %d falhou ao configurar SIGALRM", doctor_id);
        detach_shared_memory();
        exit(EXIT_FAILURE);
    }
    
    // Configurar handler para SIGTERM (terminação pelo pai)
    struct sigaction sa_term;
    sa_term.sa_handler = sigalrm_handler; // Reutilizar handler de fim de turno
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        write_log("ERRO: Doctor %d falhou ao configurar SIGTERM", doctor_id);
        detach_shared_memory();
        exit(EXIT_FAILURE);
    }
    
    write_log("Doctor %d: Sinais configurados (SIGALRM, SIGTERM)", doctor_id);
    
    // Configurar alarme para o fim do turno
    alarm(config->shift_length);
    
    write_log("Doctor %d: Turno iniciado", doctor_id);
    
    // Loop principal do Doctor
    while (shift_active) {
        Patient patient;
        
        // Tentar obter paciente da fila (prioridade 0 = qualquer, por ordem de urgência)
        if (receive_patient_from_queue(&patient, 0) == 0) {
            struct timespec attendance_start, attendance_end;
            
            if (clock_gettime(CLOCK_REALTIME, &attendance_start) != 0) {
                write_log("ERRO: Doctor %d falhou ao obter timestamp inicial", doctor_id);
                continue;
            }
            
            write_log("Doctor %d: Início atendimento - Paciente %s (prioridade %d)",
                     doctor_id, patient.name, patient.priority);
            
            if (clock_gettime(CLOCK_REALTIME, &attendance_end) != 0) {
                write_log("ERRO: Doctor %d falhou ao obter timestamp final", doctor_id);
                continue;
            }
            
            write_log("Doctor %d: Fim atendimento - Paciente %s", doctor_id, patient.name);
            
            // Calcular tempos para estatísticas
            double wait_time = (attendance_start.tv_sec - patient.triage_end.tv_sec) +
                              (attendance_start.tv_nsec - patient.triage_end.tv_nsec) / 1e9;
            
            double total_time = (attendance_end.tv_sec - patient.arrival_time.tv_sec) +
                               (attendance_end.tv_nsec - patient.arrival_time.tv_nsec) / 1e9;
            
            // Atualizar estatísticas
            update_attended_stats(wait_time, total_time);
        }
    }
    
    write_log("Doctor %d: Turno terminado (PID: %d)", doctor_id, getpid());
    
    // Desanexar da memória partilhada
    detach_shared_memory();
    
    exit(EXIT_SUCCESS);
}

/*
 * Função principal executada por cada processo Doctor temporário
 */
void temporary_doctor_main(int doctor_id, const Config *config) {
    write_log("Doctor TEMP-%d: Processo temporário iniciado (PID: %d)", doctor_id, getpid());
    
    // Bloquear sinais indesejados
    block_unwanted_signals_doctor();
    
    // Anexar à memória partilhada
    if (attach_shared_memory() != 0) {
        write_log("ERRO: Doctor TEMP-%d falhou ao anexar à memória partilhada", doctor_id);
        exit(EXIT_FAILURE);
    }
    
    // Obter acesso à fila de mensagens existente
    key_t key = ftok(MSQ_KEY_PATH, MSQ_KEY_ID);
    if (key == -1) {
        write_log("ERRO: Doctor TEMP-%d falhou ao gerar chave para fila de mensagens", doctor_id);
        detach_shared_memory();
        exit(EXIT_FAILURE);
    }
    
    msq_id = msgget(key, 0666);
    if (msq_id == -1) {
        write_log("ERRO: Doctor TEMP-%d falhou ao aceder à fila de mensagens", doctor_id);
        detach_shared_memory();
        exit(EXIT_FAILURE);
    }
    
    // Configurar handler para SIGTERM
    struct sigaction sa_term;
    sa_term.sa_handler = sigalrm_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        write_log("ERRO: Doctor TEMP-%d falhou ao configurar SIGTERM", doctor_id);
        detach_shared_memory();
        exit(EXIT_FAILURE);
    }
    
    write_log("Doctor TEMP-%d: A trabalhar (sem turno fixo)", doctor_id);
    
    int threshold = (int)(config->msq_wait_max * 0.8); // 80% do máximo
    
    // Loop principal do Doctor temporário
    while (shift_active) {
        // Verificar se deve terminar (fila abaixo de 80%)
        int queue_size = get_queue_size();
        
        if (queue_size < 0) {
            write_log("ERRO: Doctor TEMP-%d falhou ao obter tamanho da fila", doctor_id);
            continue;
        }
        
        if (queue_size < threshold) {
            write_log("Doctor TEMP-%d: Fila abaixo de 80%% (%d < %d). A terminar...", 
                     doctor_id, queue_size, threshold);
            break;
        }
        
        Patient patient;
        
        // Tentar obter paciente da fila
        if (receive_patient_from_queue(&patient, 0) == 0) {
            struct timespec attendance_start, attendance_end;
            
            if (clock_gettime(CLOCK_REALTIME, &attendance_start) != 0) {
                write_log("ERRO: Doctor TEMP-%d falhou ao obter timestamp inicial", doctor_id);
                continue;
            }
            
            write_log("Doctor TEMP-%d: Início atendimento - Paciente %s (prioridade %d)",
                     doctor_id, patient.name, patient.priority);

            if (clock_gettime(CLOCK_REALTIME, &attendance_end) != 0) {
                write_log("ERRO: Doctor TEMP-%d falhou ao obter timestamp final", doctor_id);
                continue;
            }
            
            write_log("Doctor TEMP-%d: Fim atendimento - Paciente %s", doctor_id, patient.name);
            
            // Calcular tempos para estatísticas
            double wait_time = (attendance_start.tv_sec - patient.triage_end.tv_sec) +
                              (attendance_start.tv_nsec - patient.triage_end.tv_nsec) / 1e9;
            
            double total_time = (attendance_end.tv_sec - patient.arrival_time.tv_sec) +
                               (attendance_end.tv_nsec - patient.arrival_time.tv_nsec) / 1e9;
            
            // Atualizar estatísticas
            update_attended_stats(wait_time, total_time);
        }
    }
    
    write_log("Doctor TEMP-%d: Processo temporário terminado (PID: %d)", doctor_id, getpid());
    
    // Desanexar da memória partilhada
    detach_shared_memory();
    
    exit(EXIT_SUCCESS);
}

/*
 * Cria um único processo Doctor permanente
 * Retorna o PID do processo criado, ou -1 em caso de erro
 */
int create_doctor_process(int doctor_id, const Config *config) {
    pid_t pid = fork();
    
    if (pid < 0) {
        // Erro no fork
        perror("Erro ao criar processo Doctor");
        return -1;
    }
    else if (pid == 0) {
        // Código do processo filho (Doctor)
        doctor_main(doctor_id, config);
        // Nunca chega aqui (doctor_main termina com exit)
    }
    else {
        // Código do processo pai (Admission)
        #ifdef DEBUG
        printf("[DEBUG] Doctor %d criado com PID %d\n", doctor_id, pid);
        #endif
        
        // Guardar informação do doctor
        if (doctors_array != NULL) {
            doctors_array[doctor_id - 1].pid = pid;
            doctors_array[doctor_id - 1].id = doctor_id;
            doctors_array[doctor_id - 1].start_time = time(NULL);
            doctors_array[doctor_id - 1].is_temporary = 0;
        }
        
        return pid;
    }
    return -1;
}

/*
 * Cria um processo Doctor temporário
 * Retorna o PID do processo criado, ou -1 em caso de erro
 */
int create_temporary_doctor(const Config *config) {
    temporary_doctor_counter++;
    int temp_id = temporary_doctor_counter;
    
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Erro ao criar processo Doctor temporário");
        return -1;
    }
    else if (pid == 0) {
        // Código do processo filho (Doctor temporário)
        temporary_doctor_main(temp_id, config);
        // Nunca chega aqui
    }
    else {
        // Código do processo pai (Admission)
        num_temporary_doctors++;
        
        write_log("Doctor temporário TEMP-%d criado (PID: %d). Total temporários: %d", 
                 temp_id, pid, num_temporary_doctors);
        
        return pid;
    }
    
    return -1;
}

/*
 * Verifica se deve criar um doctor temporário
 */
void check_and_create_temporary_doctor(const Config *config) {
    int queue_size = get_queue_size();
    
    if (queue_size >= config->msq_wait_max) {
        write_log("ALERTA: Fila de atendimento atingiu o máximo (%d >= %d)", 
                 queue_size, config->msq_wait_max);
        
        // Criar doctor temporário
        if (create_temporary_doctor(config) > 0) {
            write_log("Doctor temporário criado para ajudar com a sobrecarga");
        } else {
            write_log("ERRO: Falha ao criar doctor temporário");
        }
    }
}

/*
 * Cria todos os processos Doctor definidos na configuração
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int create_all_doctors(const Config *config) {
    if (config->doctors <= 0) {
        fprintf(stderr, "ERRO: Número de doctors inválido\n");
        return -1;
    }
    
    // Alocar array para guardar informação dos doctors
    doctors_array = (DoctorInfo *)malloc(config->doctors * sizeof(DoctorInfo));
    if (doctors_array == NULL) {
        perror("Erro ao alocar memória para doctors_array");
        return -1;
    }
    
    // Inicializar array
    memset(doctors_array, 0, config->doctors * sizeof(DoctorInfo));
    
    printf("\n=== Criação dos Processos Doctor ===\n");
    
    // Criar cada processo Doctor
    for (int i = 0; i < config->doctors; i++) {
        int doctor_id = i + 1; // IDs começam em 1
        
        pid_t pid = create_doctor_process(doctor_id, config);
        
        if (pid < 0) {
            fprintf(stderr, "ERRO: Falha ao criar Doctor %d\n", doctor_id);
            // Tentar terminar os doctors já criados
            terminate_all_doctors();
            free(doctors_array);
            doctors_array = NULL;
            return -1;
        }
    }
    
    printf("=== %d Processos Doctor criados com sucesso ===\n\n", config->doctors);
    
    return 0;
}

/*
 * Termina todos os processos Doctor
 */
void terminate_all_doctors() {
    if (doctors_array == NULL) {
        return;
    }
    
    printf("\n=== Terminação dos Processos Doctor ===\n");
    
    // Enviar SIGTERM para todos os doctors
    for (int i = 0; doctors_array[i].pid != 0; i++) {
        if (doctors_array[i].pid > 0) {
            #ifdef DEBUG
            printf("[DEBUG] A enviar SIGTERM para Doctor %d (PID: %d)\n", 
                   doctors_array[i].id, doctors_array[i].pid);
            #endif
            kill(doctors_array[i].pid, SIGTERM);
        }
    }
    
    // Aguardar pela terminação de todos os doctors
    int status;
    pid_t pid;
    while ((pid = wait(&status)) > 0) {
        // Encontrar qual doctor terminou
        for (int i = 0; doctors_array[i].pid != 0; i++) {
            if (doctors_array[i].pid == pid) {
                printf("[Doctor %d] Processo terminado (PID: %d)\n", 
                       doctors_array[i].id, pid);
                break;
            }
        }
    }
    
    printf("=== Todos os Processos Doctor terminaram ===\n\n");
    
    // Libertar memória
    free(doctors_array);
    doctors_array = NULL;
}
