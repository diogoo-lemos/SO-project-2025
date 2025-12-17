/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#ifndef LOG_H
#define LOG_H

#include <sys/types.h>

#define LOG_FILENAME "DEI_Emergency.log"
#define LOG_FILE_SIZE (10 * 1024 * 1024)  // 10 MB - tamanho suficiente para evitar remapping

/* Variáveis globais para o log */
extern char *log_buffer;
extern size_t log_current_pos;
extern int log_fd;

/* Funções para gestão do log */
int create_log_file();
void write_log(const char *format, ...);
void close_log_file();

#endif // LOG_H