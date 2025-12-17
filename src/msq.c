/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "msq.h"

#define DEBUG 

/* Variável global para o ID da fila de mensagens */
int msq_id = -1;

/*
 * Cria a fila de mensagens
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int create_message_queue() {
    #ifdef DEBUG
    printf("[DEBUG] A criar fila de mensagens...\n");
    #endif
    
    // Gerar chave única para a fila de mensagens
    key_t key = ftok(MSQ_KEY_PATH, MSQ_KEY_ID);
    if (key == -1) {
        perror("Erro ao gerar chave para fila de mensagens (ftok)");
        return -1;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] Chave gerada: 0x%x\n", key);
    #endif
    
    // Remover fila anterior se existir
    int old_msq = msgget(key, 0666);
    if (old_msq != -1) {
        msgctl(old_msq, IPC_RMID, NULL);
        #ifdef DEBUG
        printf("[DEBUG] Fila de mensagens anterior removida\n");
        #endif
    }
    
    // Criar nova fila de mensagens
    msq_id = msgget(key, IPC_CREAT | IPC_EXCL | 0666);
    if (msq_id == -1) {
        perror("Erro ao criar fila de mensagens (msgget)");
        return -1;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] Fila de mensagens criada com sucesso (ID: %d)\n", msq_id);
    #endif
    
    printf("Fila de mensagens criada (ID: %d)\n", msq_id);
    
    return 0;
}

/*
 * Envia um paciente para a fila de mensagens
 * A prioridade do paciente determina o tipo da mensagem (mtype)
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int send_patient_to_queue(const Patient *patient) {
    if (msq_id == -1) {
        fprintf(stderr, "ERRO: Fila de mensagens não inicializada\n");
        return -1;
    }
    
    if (patient == NULL) {
        fprintf(stderr, "ERRO: Paciente NULL\n");
        return -1;
    }
    
    PatientMessage msg;
    msg.mtype = patient->priority; // Prioridade 1-5
    memcpy(&msg.patient, patient, sizeof(Patient));
    
    #ifdef DEBUG
    printf("[DEBUG] A enviar paciente %s (prioridade %ld) para fila...\n", 
           patient->name, msg.mtype);
    #endif
    
    // Enviar mensagem (IPC_NOWAIT para não bloquear)
    if (msgsnd(msq_id, &msg, sizeof(Patient), IPC_NOWAIT) == -1) {
        if (errno == EAGAIN) {
            fprintf(stderr, "ERRO: Fila de mensagens cheia\n");
        } else {
            perror("Erro ao enviar paciente para fila (msgsnd)");
        }
        return -1;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] Paciente %s enviado para fila com sucesso\n", patient->name);
    #endif
    
    return 0;
}

/*
 * Recebe um paciente da fila de mensagens
 * Se priority > 0, recebe apenas mensagens dessa prioridade
 * Se priority = 0, recebe a mensagem com menor mtype (maior prioridade)
 * Se priority < 0, recebe qualquer mensagem com mtype <= |priority| 
 * Retorna 0 em caso de sucesso, -1 em caso de erro ou sem mensagens
 */
int receive_patient_from_queue(Patient *patient, long priority) {
    if (msq_id == -1) {
        fprintf(stderr, "ERRO: Fila de mensagens não inicializada\n");
        return -1;
    }
    
    if (patient == NULL) {
        fprintf(stderr, "ERRO: Paciente NULL\n");
        return -1;
    }
    
    PatientMessage msg;
    
    // Usar prioridade negativa para obter pacientes por ordem de prioridade
    // msgtyp = -5 significa: receber mensagem com menor mtype (1,2,3,4,5)
    // Isto garante que prioridade 1 (mais urgente) é atendida primeiro
    long msgtyp = (priority == 0) ? -5 : priority;
    
    // Receber mensagem (IPC_NOWAIT para não bloquear)
    ssize_t result = msgrcv(msq_id, &msg, sizeof(Patient), msgtyp, IPC_NOWAIT);
    
    if (result == -1) {
        if (errno == ENOMSG) {
            // Não há mensagens disponíveis
            return -1;
        } else {
            perror("Erro ao receber paciente da fila (msgrcv)");
            return -1;
        }
    }
    
    memcpy(patient, &msg.patient, sizeof(Patient));
    
    #ifdef DEBUG
    printf("[DEBUG] Paciente %s recebido da fila (prioridade %ld)\n", 
           patient->name, msg.mtype);
    #endif
    
    return 0;
}

/*
 * Obtém o número de mensagens na fila
 * Retorna o número de mensagens, ou -1 em caso de erro
 */
int get_queue_size() {
    if (msq_id == -1) {
        fprintf(stderr, "ERRO: Fila de mensagens não inicializada\n");
        return -1;
    }
    
    struct msqid_ds buf;
    
    if (msgctl(msq_id, IPC_STAT, &buf) == -1) {
        perror("Erro ao obter estatísticas da fila (msgctl)");
        return -1;
    }
    
    return (int)buf.msg_qnum;
}

/*
 * Destrói a fila de mensagens
 */
void destroy_message_queue() {
    if (msq_id == -1) {
        return;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] A destruir fila de mensagens (ID: %d)...\n", msq_id);
    #endif
    
    // Obter estatísticas finais
    struct msqid_ds buf;
    if (msgctl(msq_id, IPC_STAT, &buf) != -1) {
        #ifdef DEBUG
        printf("[DEBUG] Mensagens restantes na fila: %lu\n", buf.msg_qnum);
        #endif
        if (buf.msg_qnum > 0) {
            printf("Aviso: %lu mensagens ainda na fila\n", buf.msg_qnum);
        }
    }
    
    // Remover a fila de mensagens
    if (msgctl(msq_id, IPC_RMID, NULL) == -1) {
        perror("Erro ao destruir fila de mensagens (msgctl)");
    } else {
        #ifdef DEBUG
        printf("[DEBUG] Fila de mensagens destruída com sucesso\n");
        #endif
    }
    
    msq_id = -1;
}