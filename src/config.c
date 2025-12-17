/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

#define DEBUG 

/* 
 * Carrega as configurações a partir de um ficheiro
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int load_config(const char *filename, Config *config) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Erro ao abrir ficheiro de configuração");
        return -1;
    }

    char line[256];
    int loaded = 0;

    // Inicializar config 
    config->triage_queue_max = 0;
    config->triage = 0;
    config->doctors = 0;
    config->shift_length = 0;
    config->msq_wait_max = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        // Remover comentários e linhas vazias
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // Parsear cada parâmetro
        if (sscanf(line, "TRIAGE_QUEUE_MAX = %d", &config->triage_queue_max) == 1) {
            #ifdef DEBUG
            printf("[DEBUG] TRIAGE_QUEUE_MAX = %d\n", config->triage_queue_max);
            #endif
            loaded++;
        }
        else if (sscanf(line, "TRIAGE = %d", &config->triage) == 1 ||
                 sscanf(line, "TRIAGE= %d", &config->triage) == 1) {
            #ifdef DEBUG
            printf("[DEBUG] TRIAGE = %d\n", config->triage);
            #endif
            loaded++;
        }
        else if (sscanf(line, "DOCTORS = %d", &config->doctors) == 1 ||
                 sscanf(line, "DOCTORS= %d", &config->doctors) == 1) {
            #ifdef DEBUG
            printf("[DEBUG] DOCTORS = %d\n", config->doctors);
            #endif
            loaded++;
        }
        else if (sscanf(line, "SHIFT_LENGTH = %d", &config->shift_length) == 1 ||
                 sscanf(line, "SHIFT_LENGTH= %d", &config->shift_length) == 1) {
            #ifdef DEBUG
            printf("[DEBUG] SHIFT_LENGTH = %d\n", config->shift_length);
            #endif
            loaded++;
        }
        else if (sscanf(line, "MSQ_WAIT_MAX = %d", &config->msq_wait_max) == 1 ||
                 sscanf(line, "MSQ_WAIT_MAX= %d", &config->msq_wait_max) == 1) {
            #ifdef DEBUG
            printf("[DEBUG] MSQ_WAIT_MAX = %d\n", config->msq_wait_max);
            #endif
            loaded++;
        }
    }

    fclose(file);

    // Validar se todos os parâmetros foram carregados
    if (loaded != 5) {
        fprintf(stderr, "ERRO: Ficheiro de configuração incompleto (carregados %d/5 parâmetros)\n", loaded);
        perror("Falha ao carregar configuração completa");
        return -1;
    }

    // Validar valores
    if (config->triage_queue_max <= 0 || config->triage <= 0 || 
        config->doctors <= 0 || config->shift_length <= 0 || 
        config->msq_wait_max <= 0) {
        fprintf(stderr, "ERRO: Valores de configuração inválidos (devem ser > 0)\n");
        return -1;
    }

    return 0;
}

/*
 * Imprime as configurações carregadas
 */
void print_config(const Config *config) {
    printf("=== Configuração do Sistema ===\n");
    printf("TRIAGE_QUEUE_MAX: %d\n", config->triage_queue_max);
    printf("TRIAGE: %d\n", config->triage);
    printf("DOCTORS: %d\n", config->doctors);
    printf("SHIFT_LENGTH: %d segundos\n", config->shift_length);
    printf("MSQ_WAIT_MAX: %d\n", config->msq_wait_max);
    printf("================================\n");
}