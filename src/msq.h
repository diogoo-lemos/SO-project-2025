/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef MSQ_H
#define MSQ_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "patient.h"

#define MSQ_KEY_PATH "/tmp"
#define MSQ_KEY_ID 'U'  

/* Estrutura da mensagem para a fila */
typedef struct {
    long mtype;              // Tipo da mensagem (prioridade: 1-5)
    Patient patient;         // Dados do paciente
} PatientMessage;

/* Variável global para o ID da fila de mensagens */
extern int msq_id;

/* Funções para gestão da fila de mensagens */
int create_message_queue();
int send_patient_to_queue(const Patient *patient);
int receive_patient_from_queue(Patient *patient, long priority);
int get_queue_size();
void destroy_message_queue();

#endif // MSQ_H