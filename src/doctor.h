/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef DOCTOR_H
#define DOCTOR_H

#include <sys/types.h>
#include "config.h"

/* Estrutura para guardar informação de um processo Doctor */
typedef struct {
    pid_t pid;           // PID do processo
    int id;              // ID do doctor (1, 2, 3, ...)
    time_t start_time;   // Hora de início do turno
    int is_temporary;    // Flag: 1 = temporário, 0 = permanente
} DoctorInfo;

/* Array global para guardar informação dos doctors */
extern DoctorInfo *doctors_array;

/* Informação sobre doctors temporários */
extern int num_temporary_doctors;
extern int temporary_doctor_counter;

/* Funções para gestão dos processos Doctor */
int create_doctor_process(int doctor_id, const Config *config);
void doctor_main(int doctor_id, const Config *config);
void temporary_doctor_main(int doctor_id, const Config *config);
int create_all_doctors(const Config *config);
void terminate_all_doctors();

/* Funções para doctors temporários */
int create_temporary_doctor(const Config *config);
void check_and_create_temporary_doctor(const Config *config);

#endif // DOCTOR_H