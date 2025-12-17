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
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "shm.h"

#define DEBUG 

#define SHM_NAME "/urgencias_shm"
#define SHM_SIZE sizeof(Statistics)

/* Variáveis globais */
Statistics *shm_stats = NULL;
int shm_fd = -1;

/*
 * Cria e inicializa a memória partilhada
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int create_shared_memory() {
    #ifdef DEBUG
    printf("[DEBUG] A criar memória partilhada...\n");
    #endif
    
    // Remover memória partilhada anterior (se existir)
    shm_unlink(SHM_NAME);
    
    // Criar objeto de memória partilhada
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Erro ao criar memória partilhada (shm_open)");
        return -1;
    }
    
    // Definir o tamanho da memória partilhada
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("Erro ao definir tamanho da memória partilhada (ftruncate)");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return -1;
    }
    
    // Mapear a memória partilhada
    shm_stats = (Statistics *)mmap(NULL, SHM_SIZE, 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, shm_fd, 0);
    
    if (shm_stats == MAP_FAILED) {
        perror("Erro ao mapear memória partilhada (mmap)");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        return -1;
    }
    
    // Inicializar a estrutura de estatísticas
    memset(shm_stats, 0, SHM_SIZE);
    
    // Inicializar o mutex com atributos para memória partilhada
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    
    if (pthread_mutex_init(&shm_stats->mutex, &mutex_attr) != 0) {
        perror("Erro ao inicializar mutex");
        munmap(shm_stats, SHM_SIZE);
        close(shm_fd);
        shm_unlink(SHM_NAME);
        pthread_mutexattr_destroy(&mutex_attr);
        return -1;
    }
    
    pthread_mutexattr_destroy(&mutex_attr);
    
    #ifdef DEBUG
    printf("[DEBUG] Memória partilhada criada com sucesso\n");
    printf("[DEBUG] Nome: %s\n", SHM_NAME);
    printf("[DEBUG] Tamanho: %lu bytes\n", SHM_SIZE);
    printf("[DEBUG] Endereço: %p\n", (void *)shm_stats);
    #endif
    
    printf("Memória partilhada criada e inicializada.\n");
    
    return 0;
}

/*
 * Anexa à memória partilhada existente (usado pelos processos Doctor)
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int attach_shared_memory() {
    #ifdef DEBUG
    printf("[DEBUG] A anexar à memória partilhada...\n");
    #endif
    
    // Abrir objeto de memória partilhada existente
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Erro ao abrir memória partilhada (shm_open)");
        return -1;
    }
    
    // Mapear a memória partilhada
    shm_stats = (Statistics *)mmap(NULL, SHM_SIZE, 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, shm_fd, 0);
    
    if (shm_stats == MAP_FAILED) {
        perror("Erro ao mapear memória partilhada (mmap)");
        close(shm_fd);
        return -1;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] Anexado à memória partilhada com sucesso\n");
    #endif
    
    return 0;
}

/*
 * Desanexa da memória partilhada
 */
void detach_shared_memory() {
    if (shm_stats != NULL && shm_stats != MAP_FAILED) {
        #ifdef DEBUG
        printf("[DEBUG] A desanexar da memória partilhada...\n");
        #endif
        
        munmap(shm_stats, SHM_SIZE);
        shm_stats = NULL;
    }
    
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
}

/*
 * Destrói a memória partilhada (apenas o processo criador deve chamar)
 */
void destroy_shared_memory() {
    #ifdef DEBUG
    printf("[DEBUG] A destruir memória partilhada...\n");
    #endif
    
    // Destruir o mutex
    if (shm_stats != NULL && shm_stats != MAP_FAILED) {
        pthread_mutex_destroy(&shm_stats->mutex);
    }
    
    // Desanexar
    detach_shared_memory();
    
    // Remover o objeto de memória partilhada
    if (shm_unlink(SHM_NAME) == -1) {
        perror("Aviso: Erro ao remover memória partilhada (shm_unlink)");
    } else {
        #ifdef DEBUG
        printf("[DEBUG] Memória partilhada destruída com sucesso\n");
        #endif
    }
}

/*
 * Atualiza estatísticas após triagem de um paciente
 */
void update_triaged_stats(double wait_time) {
    if (shm_stats == NULL) {
        fprintf(stderr, "ERRO: Memória partilhada não inicializada\n");
        return;
    }
    
    pthread_mutex_lock(&shm_stats->mutex);
    
    shm_stats->total_triaged++;
    shm_stats->total_wait_triage += wait_time;
    
    #ifdef DEBUG
    printf("[DEBUG] Estatísticas atualizadas: triaged=%d, wait_triage=%.2f\n",
           shm_stats->total_triaged, shm_stats->total_wait_triage);
    #endif
    
    pthread_mutex_unlock(&shm_stats->mutex);
}

/*
 * Atualiza estatísticas após atendimento de um paciente
 */
void update_attended_stats(double wait_time, double total_time) {
    if (shm_stats == NULL) {
        fprintf(stderr, "ERRO: Memória partilhada não inicializada\n");
        return;
    }
    
    pthread_mutex_lock(&shm_stats->mutex);
    
    shm_stats->total_attended++;
    shm_stats->total_wait_doctor += wait_time;
    shm_stats->total_time_system += total_time;
    
    #ifdef DEBUG
    printf("[DEBUG] Estatísticas atualizadas: attended=%d, wait_doctor=%.2f, time_system=%.2f\n",
           shm_stats->total_attended, shm_stats->total_wait_doctor, shm_stats->total_time_system);
    #endif
    
    pthread_mutex_unlock(&shm_stats->mutex);
}

/*
 * Imprime as estatísticas atuais
 */
void print_statistics() {
    if (shm_stats == NULL) {
        fprintf(stderr, "ERRO: Memória partilhada não inicializada\n");
        return;
    }
    
    pthread_mutex_lock(&shm_stats->mutex);
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              ESTATÍSTICAS DO SISTEMA                       ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║ Número total de pacientes triados:            %10d ║\n", shm_stats->total_triaged);
    printf("║ Número total de pacientes atendidos:          %10d ║\n", shm_stats->total_attended);
    
    if (shm_stats->total_triaged > 0) {
        double avg_wait_triage = shm_stats->total_wait_triage / shm_stats->total_triaged;
        printf("║ Tempo médio de espera antes da triagem:       %10.2f s ║\n", avg_wait_triage);
    } else {
        printf("║ Tempo médio de espera antes da triagem:              N/A ║\n");
    }
    
    if (shm_stats->total_attended > 0) {
        double avg_wait_doctor = shm_stats->total_wait_doctor / shm_stats->total_attended;
        double avg_total_time = shm_stats->total_time_system / shm_stats->total_attended;
        printf("║ Tempo médio entre triagem e atendimento:      %10.2f s ║\n", avg_wait_doctor);
        printf("║ Tempo médio total no sistema:                 %10.2f s ║\n", avg_total_time);
    } else {
        printf("║ Tempo médio entre triagem e atendimento:             N/A ║\n");
        printf("║ Tempo médio total no sistema:                        N/A ║\n");
    }
    
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    pthread_mutex_unlock(&shm_stats->mutex);
}