/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef PIPE_H
#define PIPE_H

#define PIPE_NAME "/tmp/input_pipe"
#define PIPE_BUFFER_SIZE 256

/* Funções para gestão do named pipe */
int create_named_pipe();
int open_named_pipe_read();
void close_named_pipe(int fd);
void destroy_named_pipe();

#endif // PIPE_H