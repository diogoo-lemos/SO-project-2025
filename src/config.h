/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Estrutura para guardar as configurações do sistema */
typedef struct {
    int triage_queue_max;    // Tamanho máximo da fila de triagem
    int triage;              // Número de threads de triagem
    int doctors;             // Número de processos doctor
    int shift_length;        // Duração do turno em segundos
    int msq_wait_max;        // Tamanho máximo da fila de atendimento
} Config;

/* Funções para manipular configurações */
int load_config(const char *filename, Config *config);
void print_config(const Config *config);

#endif // CONFIG_H