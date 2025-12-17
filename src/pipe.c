/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "pipe.h"

#define DEBUG 

/*
 * Cria o named pipe
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int create_named_pipe() {
    #ifdef DEBUG
    printf("[DEBUG] A criar named pipe '%s'...\n", PIPE_NAME);
    #endif
    
    // Remover named pipe anterior (se existir)
    unlink(PIPE_NAME);
    
    // Criar o named pipe com permissões 0666
    if (mkfifo(PIPE_NAME, 0666) == -1) {
        if (errno != EEXIST) {
            perror("Erro ao criar named pipe (mkfifo)");
            return -1;
        }
        #ifdef DEBUG
        printf("[DEBUG] Named pipe já existe, a reutilizar...\n");
        #endif
    }
    
    #ifdef DEBUG
    printf("[DEBUG] Named pipe '%s' criado com sucesso\n", PIPE_NAME);
    #endif
    
    printf("Named pipe criado: %s\n", PIPE_NAME);
    
    return 0;
}

/*
 * Abre o named pipe para leitura (não-bloqueante)
 * Retorna o file descriptor, ou -1 em caso de erro
 */
int open_named_pipe_read() {
    #ifdef DEBUG
    printf("[DEBUG] A abrir named pipe para leitura...\n");
    #endif
    
    // Abrir em modo não-bloqueante para não ficar preso se não houver escritores
    int fd = open(PIPE_NAME, O_RDONLY | O_NONBLOCK);
    
    if (fd == -1) {
        perror("Erro ao abrir named pipe para leitura");
        return -1;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] Named pipe aberto (fd: %d)\n", fd);
    #endif
    
    return fd;
}

/*
 * Fecha o named pipe
 */
void close_named_pipe(int fd) {
    if (fd != -1) {
        #ifdef DEBUG
        printf("[DEBUG] A fechar named pipe (fd: %d)...\n", fd);
        #endif
        close(fd);
    }
}

/*
 * Destrói o named pipe
 */
void destroy_named_pipe() {
    #ifdef DEBUG
    printf("[DEBUG] A destruir named pipe '%s'...\n", PIPE_NAME);
    #endif
    
    if (unlink(PIPE_NAME) == -1) {
        if (errno != ENOENT) {
            perror("Aviso: Erro ao remover named pipe");
        }
    } else {
        #ifdef DEBUG
        printf("[DEBUG] Named pipe destruído com sucesso\n");
        #endif
    }
}