/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "triage.h"
#include "config.h"
#include "shm.h"
#include "msq.h"
#include "patient.h"
#include "log.h"
#include <pthread.h>

#define DEBUG 

/* Variáveis globais */
TriageQueue *triage_queue = NULL;
TriageThreadInfo *triage_threads = NULL;
int num_triage_threads = 0;
volatile int triage_system_running = 1;

/* Mutex para controlar alterações no número de threads */
pthread_mutex_t triage_control_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Configuração global (necessária para as threads) */
static const Config *global_triage_config = NULL;

/*
 * Cria a fila de triagem
 */
TriageQueue* create_triage_queue(int capacity) {
    #ifdef DEBUG
    printf("[DEBUG] A criar fila de triagem com capacidade %d...\n", capacity);
    #endif
    
    TriageQueue *queue = (TriageQueue *)malloc(sizeof(TriageQueue));
    if (queue == NULL) {
        perror("Erro ao alocar memória para fila de triagem");
        return NULL;
    }
    
    queue->patients = (Patient **)malloc(capacity * sizeof(Patient *));
    if (queue->patients == NULL) {
        perror("Erro ao alocar memória para array de pacientes");
        free(queue);
        return NULL;
    }
    
    queue->front = 0;
    queue->rear = -1;
    queue->count = 0;
    queue->capacity = capacity;
    
    // Inicializar mutex e variáveis de condição
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        perror("Erro ao inicializar mutex da fila de triagem");
        free(queue->patients);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        perror("Erro ao inicializar variável de condição not_empty");
        pthread_mutex_destroy(&queue->mutex);
        free(queue->patients);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        perror("Erro ao inicializar variável de condição not_full");
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->patients);
        free(queue);
        return NULL;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] Fila de triagem criada com sucesso\n");
    #endif
    
    return queue;
}

/*
 * Adiciona um paciente à fila de triagem
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int enqueue_patient(TriageQueue *queue, Patient *patient) {
    if (queue == NULL || patient == NULL) {
        fprintf(stderr, "ERRO: Fila ou paciente NULL\n");
        return -1;
    }
    
    // Tentar obter lock com timeout ✅ MELHORADO
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5; // 5 segundos de timeout
    
    int lock_result = pthread_mutex_lock(&queue->mutex);
    if (lock_result != 0) {
        fprintf(stderr, "ERRO: Falha ao obter mutex (erro %d)\n", lock_result);
        return -1;
    }
    
    // Verificar se a fila está cheia
    if (queue->count >= queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        fprintf(stderr, "ERRO: Fila de triagem cheia! Paciente %s descartado.\n", 
                patient->name);
        write_log("ERRO: Fila de triagem cheia! Paciente %s descartado.", patient->name);
        return -1;
    }
    
    // Adicionar paciente à fila
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->patients[queue->rear] = patient;
    queue->count++;
    
    #ifdef DEBUG
    printf("[DEBUG] Paciente %s adicionado à fila de triagem (posição %d/%d)\n",
           patient->name, queue->count, queue->capacity);
    #endif
    
    // Sinalizar que a fila não está vazia
    if (pthread_cond_signal(&queue->not_empty) != 0) {
        write_log("AVISO: Falha ao sinalizar condição not_empty");
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

/*
 * Remove e retorna um paciente da fila de triagem
 * Bloqueia se a fila estiver vazia
 */
Patient* dequeue_patient(TriageQueue *queue) {
    if (queue == NULL) {
        fprintf(stderr, "ERRO: Fila NULL\n");
        return NULL;
    }
    
    int lock_result = pthread_mutex_lock(&queue->mutex);
    if (lock_result != 0) {
        fprintf(stderr, "ERRO: Falha ao obter mutex (erro %d)\n", lock_result);
        return NULL;
    }
    
    // Aguardar enquanto a fila está vazia e o sistema está a correr
    while (queue->count == 0 && triage_system_running) {
        int wait_result = pthread_cond_wait(&queue->not_empty, &queue->mutex);
        if (wait_result != 0) {
            write_log("ERRO: Falha em pthread_cond_wait (erro %d)", wait_result);
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
    }
    
    // Se o sistema está a terminar e a fila está vazia, retornar NULL
    if (!triage_system_running && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    
    // Remover paciente da fila
    Patient *patient = queue->patients[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->count--;
    
    #ifdef DEBUG
    printf("[DEBUG] Paciente %s removido da fila de triagem (%d restantes)\n",
           patient->name, queue->count);
    #endif
    
    // Sinalizar que a fila não está cheia
    if (pthread_cond_signal(&queue->not_full) != 0) {
        write_log("AVISO: Falha ao sinalizar condição not_full");
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    return patient;
}

/*
 * Destrói a fila de triagem
 */
void destroy_triage_queue(TriageQueue *queue) {
    if (queue == NULL) {
        return;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] A destruir fila de triagem...\n");
    #endif
    
    pthread_mutex_lock(&queue->mutex);
    
    // Libertar pacientes restantes na fila
    if (queue->count > 0) {
        printf("Aviso: %d pacientes ainda na fila de triagem\n", queue->count);
        for (int i = 0; i < queue->count; i++) {
            int index = (queue->front + i) % queue->capacity;
            if (queue->patients[index] != NULL) {
                free_patient(queue->patients[index]);
            }
        }
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    // Destruir sincronização
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    
    // Libertar memória
    free(queue->patients);
    free(queue);
    
    #ifdef DEBUG
    printf("[DEBUG] Fila de triagem destruída\n");
    #endif
}

/*
 * Função executada por cada thread de triagem
 */
void* triage_thread_function(void *arg) {
    int thread_id = *((int *)arg);
    free(arg);
    
    write_log("Thread de triagem %d iniciada (TID: %lu)", thread_id, pthread_self());
    
    while (triage_system_running) {
        // Verificar se esta thread deve terminar (por redução dinâmica)
        pthread_mutex_lock(&triage_control_mutex);
        int should_terminate = 0;
        if (thread_id > num_triage_threads) {
            should_terminate = 1;
        }
        // Verificar também através do array
        if (thread_id <= num_triage_threads && 
            triage_threads != NULL && 
            triage_threads[thread_id - 1].active == 0) {
            should_terminate = 1;
        }
        pthread_mutex_unlock(&triage_control_mutex);
        
        if (should_terminate) {
            write_log("Thread de triagem %d a terminar (redução dinâmica)", thread_id);
            break;
        }
        
        // Obter paciente da fila
        Patient *patient = dequeue_patient(triage_queue);
        
        if (patient == NULL) {
            // Sistema está a terminar
            break;
        }
        
        // Registar início da triagem
        clock_gettime(CLOCK_REALTIME, &patient->triage_start);
        
        write_log("TRIAGEM %d: Início - Paciente %s", thread_id, patient->name);
        
        // Registar fim da triagem
        clock_gettime(CLOCK_REALTIME, &patient->triage_end);
        
        write_log("TRIAGEM %d: Fim - Paciente %s (prioridade %d)", 
                 thread_id, patient->name, patient->priority);
        
        // Calcular tempo de espera antes da triagem
        double wait_time = (patient->triage_start.tv_sec - patient->arrival_time.tv_sec) +
                          (patient->triage_start.tv_nsec - patient->arrival_time.tv_nsec) / 1e9;
        
        // Atualizar estatísticas de triagem
        update_triaged_stats(wait_time);
        
        // Enviar paciente para a fila de mensagens (atendimento)
        if (send_patient_to_queue(patient) != 0) {
            write_log("ERRO TRIAGEM %d: Falha ao enviar paciente %s para fila de atendimento",
                     thread_id, patient->name);
            free_patient(patient);
        } else {
            write_log("TRIAGEM %d: Paciente %s enviado para atendimento", 
                     thread_id, patient->name);
        }
    }
    
    write_log("Thread de triagem %d a terminar", thread_id);
    
    return NULL;
}

/*
 * Cria as threads de triagem
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int create_triage_threads(int num_threads, const Config *config) {
    if (num_threads <= 0) {
        fprintf(stderr, "ERRO: Número de threads inválido\n");
        return -1;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] A criar %d threads de triagem...\n", num_threads);
    #endif
    
    // Guardar configuração global
    global_triage_config = config;
    num_triage_threads = num_threads;
    
    // Criar fila de triagem
    triage_queue = create_triage_queue(config->triage_queue_max);
    if (triage_queue == NULL) {
        fprintf(stderr, "ERRO: Falha ao criar fila de triagem\n");
        return -1;
    }
    
    // Alocar array para informação das threads
    triage_threads = (TriageThreadInfo *)malloc(num_threads * sizeof(TriageThreadInfo));
    if (triage_threads == NULL) {
        perror("Erro ao alocar memória para triage_threads");
        destroy_triage_queue(triage_queue);
        triage_queue = NULL;
        return -1;
    }
    
    memset(triage_threads, 0, num_threads * sizeof(TriageThreadInfo));
    
    printf("\n=== Criação das Threads de Triagem ===\n");
    
    // Criar cada thread
    for (int i = 0; i < num_threads; i++) {
        triage_threads[i].id = i + 1;
        triage_threads[i].active = 1;
        
        int *thread_id = malloc(sizeof(int));
        if (thread_id == NULL) {
            perror("Erro ao alocar memória para thread_id");
            continue;
        }
        *thread_id = i + 1;
        
        if (pthread_create(&triage_threads[i].thread_id, NULL, 
                          triage_thread_function, thread_id) != 0) {
            perror("Erro ao criar thread de triagem");
            free(thread_id);
            triage_threads[i].active = 0;
            continue;
        }
        
        #ifdef DEBUG
        printf("[DEBUG] Thread de triagem %d criada (TID: %lu)\n", 
               i + 1, triage_threads[i].thread_id);
        #endif
    }
    
    printf("=== %d Threads de Triagem criadas com sucesso ===\n\n", num_threads);
    
    return 0;
}


/*
 * Altera dinamicamente o número de threads de triagem
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int change_triage_threads(int new_num_threads, const Config *config) {
    if (new_num_threads <= 0 || new_num_threads > config->triage_queue_max) {
        fprintf(stderr, "ERRO: Número de threads inválido (%d). Deve estar entre 1 e triage_queue_max.\n", 
                new_num_threads);
        return -1;
    }
    
    pthread_mutex_lock(&triage_control_mutex);
    
    write_log("=== ALTERAÇÃO DE THREADS DE TRIAGEM ===");
    write_log("Threads atuais: %d", num_triage_threads);
    write_log("Threads solicitadas: %d", new_num_threads);
    
    int old_num_threads = num_triage_threads;
    
    if (new_num_threads == old_num_threads) {
        write_log("Número de threads já é %d. Nenhuma alteração necessária.", new_num_threads);
        pthread_mutex_unlock(&triage_control_mutex);
        return 0;
    }
    
    if (new_num_threads > old_num_threads) {
        // Aumentar o número de threads
        write_log("A aumentar de %d para %d threads...", old_num_threads, new_num_threads);
        
        // Realocar array de threads
        TriageThreadInfo *new_array = (TriageThreadInfo *)realloc(
            triage_threads, 
            new_num_threads * sizeof(TriageThreadInfo)
        );
        
        if (new_array == NULL) {
            perror("Erro ao realocar memória para threads de triagem");
            pthread_mutex_unlock(&triage_control_mutex);
            return -1;
        }
        
        triage_threads = new_array;
        
        // Criar novas threads
        for (int i = old_num_threads; i < new_num_threads; i++) {
            triage_threads[i].id = i + 1;
            triage_threads[i].active = 1;
            
            int *thread_id = malloc(sizeof(int));
            if (thread_id == NULL) {
                perror("Erro ao alocar memória para thread_id");
                continue;
            }
            *thread_id = i + 1;
            
            if (pthread_create(&triage_threads[i].thread_id, NULL, 
                              triage_thread_function, thread_id) != 0) {
                perror("Erro ao criar thread de triagem");
                free(thread_id);
                triage_threads[i].active = 0;
                continue;
            }
            
            write_log("Thread de triagem %d criada (TID: %lu)", 
                     i + 1, triage_threads[i].thread_id);
        }
        
        num_triage_threads = new_num_threads;
        write_log("Threads de triagem aumentadas para %d com sucesso", new_num_threads);
        
    } else {
        // Diminuir o número de threads
        write_log("A diminuir de %d para %d threads...", old_num_threads, new_num_threads);
        
        // Marcar threads excedentes como inativas
        for (int i = new_num_threads; i < old_num_threads; i++) {
            if (triage_threads[i].active) {
                triage_threads[i].active = 0;
                write_log("Thread de triagem %d marcada para terminação", triage_threads[i].id);
            }
        }
        
        // Acordar todas as threads para que verifiquem se devem terminar
        pthread_mutex_lock(&triage_queue->mutex);
        pthread_cond_broadcast(&triage_queue->not_empty);
        pthread_mutex_unlock(&triage_queue->mutex);
        
        // Aguardar que as threads excedentes terminem
        for (int i = new_num_threads; i < old_num_threads; i++) {
            if (triage_threads[i].thread_id != 0) {
                pthread_join(triage_threads[i].thread_id, NULL);
                write_log("Thread de triagem %d terminada", triage_threads[i].id);
            }
        }
        
        num_triage_threads = new_num_threads;
        
        // Redimensionar array (opcional, para libertar memória)
        TriageThreadInfo *new_array = (TriageThreadInfo *)realloc(
            triage_threads, 
            new_num_threads * sizeof(TriageThreadInfo)
        );
        
        if (new_array != NULL) {
            triage_threads = new_array;
        }
        
        write_log("Threads de triagem diminuídas para %d com sucesso", new_num_threads);
    }
    
    write_log("=== ALTERAÇÃO CONCLUÍDA ===");
    
    pthread_mutex_unlock(&triage_control_mutex);
    
    return 0;
}

/*
 * Termina todas as threads de triagem
 */
void terminate_triage_threads() {
    if (triage_threads == NULL) {
        return;
    }
    
    printf("\n=== Terminação das Threads de Triagem ===\n");
    
    // Sinalizar que o sistema está a terminar
    triage_system_running = 0;
    
    // Acordar todas as threads que possam estar bloqueadas
    pthread_mutex_lock(&triage_queue->mutex);
    pthread_cond_broadcast(&triage_queue->not_empty);
    pthread_mutex_unlock(&triage_queue->mutex);
    
    // Aguardar pela terminação de todas as threads
    for (int i = 0; i < num_triage_threads; i++) {
        if (triage_threads[i].active) {
            #ifdef DEBUG
            printf("[DEBUG] A aguardar terminação da thread de triagem %d...\n", 
                   triage_threads[i].id);
            #endif
            
            pthread_join(triage_threads[i].thread_id, NULL);
            printf("[Triage %d] Thread terminada\n", triage_threads[i].id);
        }
    }
    
    printf("=== Todas as Threads de Triagem terminaram ===\n\n");
    
    // Libertar memória
    free(triage_threads);
    triage_threads = NULL;
    
    // Destruir fila de triagem
    if (triage_queue != NULL) {
        destroy_triage_queue(triage_queue);
        triage_queue = NULL;
    }
}