/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef SHM_H
#define SHM_H

#include <sys/types.h>
#include <pthread.h>

/* Estrutura para guardar estatísticas na memória partilhada */
typedef struct {
    // Contadores
    int total_triaged;              // Número total de pacientes triados
    int total_attended;             // Número total de pacientes atendidos
    
    // Tempos acumulados (para calcular médias)
    double total_wait_triage;       // Tempo total de espera antes da triagem
    double total_wait_doctor;       // Tempo total de espera entre triagem e atendimento
    double total_time_system;       // Tempo total no sistema (chegada até saída)
    
    // Mutex para sincronização
    pthread_mutex_t mutex;
} Statistics;

/* Ponteiro global para a memória partilhada */
extern Statistics *shm_stats;
extern int shm_fd;

/* Funções para gestão da memória partilhada */
int create_shared_memory();
int attach_shared_memory();
void detach_shared_memory();
void destroy_shared_memory();

/* Funções para atualizar estatísticas */
void update_triaged_stats(double wait_time);
void update_attended_stats(double wait_time, double total_time);
void print_statistics();

#endif // SHM_H