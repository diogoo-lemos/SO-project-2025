/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef PATIENT_H
#define PATIENT_H

#include <time.h>

#define MAX_NAME_LENGTH 64

/* Estrutura para representar um paciente */
typedef struct {
    int arrival_number;           // Número de chegada (ordem)
    char name[MAX_NAME_LENGTH];   // Nome do paciente
    int triage_time;              // Tempo de triagem (ms)
    int attendance_time;          // Tempo de atendimento (ms)
    int priority;                 // Prioridade (1-5, sendo 1 mais urgente)
    
    // Timestamps para estatísticas
    struct timespec arrival_time;      // Hora de chegada
    struct timespec triage_start;      // Início da triagem
    struct timespec triage_end;        // Fim da triagem
    struct timespec attendance_start;  // Início do atendimento
    struct timespec attendance_end;    // Fim do atendimento
} Patient;

/* Funções para gestão de pacientes */
Patient* create_patient(int arrival_number, const char *name, 
                       int triage_time, int attendance_time, int priority);
void free_patient(Patient *patient);
void print_patient(const Patient *patient);

#endif // PATIENT_H