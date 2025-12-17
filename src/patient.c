/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "patient.h"

/*
 * Cria e inicializa um novo paciente
 */
Patient* create_patient(int arrival_number, const char *name, 
                       int triage_time, int attendance_time, int priority) {
    Patient *patient = (Patient *)malloc(sizeof(Patient));
    if (patient == NULL) {
        perror("Erro ao alocar memória para paciente");
        return NULL;
    }
    
    patient->arrival_number = arrival_number;
    strncpy(patient->name, name, MAX_NAME_LENGTH - 1);
    patient->name[MAX_NAME_LENGTH - 1] = '\0';
    patient->triage_time = triage_time;
    patient->attendance_time = attendance_time;
    patient->priority = priority;
    
    // Registar hora de chegada
    clock_gettime(CLOCK_REALTIME, &patient->arrival_time);
    
    // Inicializar outros timestamps
    memset(&patient->triage_start, 0, sizeof(struct timespec));
    memset(&patient->triage_end, 0, sizeof(struct timespec));
    memset(&patient->attendance_start, 0, sizeof(struct timespec));
    memset(&patient->attendance_end, 0, sizeof(struct timespec));
    
    return patient;
}

/*
 * Liberta a memória de um paciente
 */
void free_patient(Patient *patient) {
    if (patient != NULL) {
        free(patient);
    }
}

/*
 * Imprime informação de um paciente
 */
void print_patient(const Patient *patient) {
    if (patient == NULL) {
        printf("Paciente: NULL\n");
        return;
    }
    
    printf("Paciente: %s (Nº %d)\n", patient->name, patient->arrival_number);
    printf("  Triagem: %d ms, Atendimento: %d ms, Prioridade: %d\n",
           patient->triage_time, patient->attendance_time, patient->priority);
}