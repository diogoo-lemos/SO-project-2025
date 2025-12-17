/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef TRIAGE_H
#define TRIAGE_H

#include <pthread.h>
#include "config.h"
#include "patient.h"

#define TRIAGE_QUEUE_NAME_SIZE 128

/* Estrutura para a fila de triagem */
typedef struct {
    Patient **patients;          // Array de ponteiros para pacientes
    int front;                   // Índice do início da fila
    int rear;                    // Índice do fim da fila
    int count;                   // Número de pacientes na fila
    int capacity;                // Capacidade máxima da fila
    pthread_mutex_t mutex;       // Mutex para sincronização
    pthread_cond_t not_empty;    // Condição: fila não vazia
    pthread_cond_t not_full;     // Condição: fila não cheia
} TriageQueue;

/* Estrutura para informação de cada thread de triagem */
typedef struct {
    pthread_t thread_id;         // ID da thread
    int id;                      // ID da thread (1, 2, 3, ...)
    int active;                  // Flag: 1 = ativa, 0 = terminada
} TriageThreadInfo;

/* Variáveis globais */
extern TriageQueue *triage_queue;
extern TriageThreadInfo *triage_threads;
extern int num_triage_threads;
extern volatile int triage_system_running;

/* Mutex para controlar alterações no número de threads */
extern pthread_mutex_t triage_control_mutex;

/* Funções para gestão da fila de triagem */
TriageQueue* create_triage_queue(int capacity);
int enqueue_patient(TriageQueue *queue, Patient *patient);
Patient* dequeue_patient(TriageQueue *queue);
void destroy_triage_queue(TriageQueue *queue);

/* Funções para gestão das threads de triagem */
int create_triage_threads(int num_threads, const Config *config);
void* triage_thread_function(void *arg);
void terminate_triage_threads();

/* Funções para alteração dinâmica */
int change_triage_threads(int new_num_threads, const Config *config);

#endif // TRIAGE_H