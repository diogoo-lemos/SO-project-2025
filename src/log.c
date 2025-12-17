/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include "log.h"

#define DEBUG 

/* Variáveis globais */
char *log_buffer = NULL;
size_t log_current_pos = 0;
int log_fd = -1;

/* Mutex para sincronização de escrita no log */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Cria e mapeia o ficheiro de log em memória
 * Retorna 0 em caso de sucesso, -1 em caso de erro
 */
int create_log_file() {
    #ifdef DEBUG
    printf("[DEBUG] A criar ficheiro de log mapeado em memória...\n");
    #endif
    
    // Remover ficheiro anterior (se existir)
    unlink(LOG_FILENAME);
    
    // Criar ficheiro de log
    log_fd = open(LOG_FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (log_fd == -1) {
        perror("Erro ao criar ficheiro de log");
        return -1;
    }
    
    // Definir o tamanho do ficheiro
    if (ftruncate(log_fd, LOG_FILE_SIZE) == -1) {
        perror("Erro ao definir tamanho do ficheiro de log");
        close(log_fd);
        unlink(LOG_FILENAME);
        return -1;
    }
    
    // Mapear o ficheiro em memória
    log_buffer = mmap(NULL, LOG_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, log_fd, 0);
    
    if (log_buffer == MAP_FAILED) {
        perror("Erro ao mapear ficheiro de log em memória");
        close(log_fd);
        unlink(LOG_FILENAME);
        log_buffer = NULL;
        return -1;
    }
    
    // Inicializar posição
    log_current_pos = 0;
    
    // Limpar buffer
    memset(log_buffer, 0, LOG_FILE_SIZE);
    
    #ifdef DEBUG
    printf("[DEBUG] Ficheiro de log criado e mapeado com sucesso\n");
    printf("[DEBUG] Nome: %s\n", LOG_FILENAME);
    printf("[DEBUG] Tamanho: %d MB\n", LOG_FILE_SIZE / (1024 * 1024));
    printf("[DEBUG] Endereço: %p\n", (void *)log_buffer);
    #endif
    
    // Escrever cabeçalho no log
    time_t now = time(NULL);
    write_log("=== DEI Emergency System Log ===");
    write_log("Log iniciado em: %s", ctime(&now));
    write_log("================================\n");
    
    return 0;
}

/*
 * Obtém timestamp formatado
 */
static void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/*
 * Escreve uma mensagem no log (thread-safe)
 * A mensagem é escrita tanto no ficheiro mapeado como no stdout
 */
void write_log(const char *format, ...) {
    if (log_buffer == NULL) {
        fprintf(stderr, "ERRO: Log não inicializado\n");
        return;
    }
    
    pthread_mutex_lock(&log_mutex);
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    char message[1024];
    va_list args;
    
    // Formatar mensagem
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Preparar linha completa com timestamp
    char log_line[1200];
    int line_len;
    
    // Se a mensagem já tem newline no final, não adicionar timestamp
    if (strchr(message, '\n') != NULL && message[0] == '=') {
        // Linha decorativa ou especial
        line_len = snprintf(log_line, sizeof(log_line), "%s", message);
    } else {
        line_len = snprintf(log_line, sizeof(log_line), "[%s] %s\n", timestamp, message);
    }
    
    // Verificar se há espaço no buffer
    if (log_current_pos + line_len >= LOG_FILE_SIZE) {
        fprintf(stderr, "AVISO: Buffer de log cheio!\n");
        pthread_mutex_unlock(&log_mutex);
        return;
    }
    
    // Escrever no buffer mapeado
    memcpy(log_buffer + log_current_pos, log_line, line_len);
    log_current_pos += line_len;
    
    // Forçar escrita no disco (para garantir persistência)
    msync(log_buffer, log_current_pos, MS_SYNC);
    
    // Escrever também no stdout (para visualização em tempo real)
    printf("%s", log_line);
    fflush(stdout);
    
    pthread_mutex_unlock(&log_mutex);
}

/*
 * Fecha e desmapeia o ficheiro de log
 */
void close_log_file() {
    if (log_buffer == NULL) {
        return;
    }
    
    #ifdef DEBUG
    printf("[DEBUG] A fechar ficheiro de log...\n");
    #endif
    
    pthread_mutex_lock(&log_mutex);
    
    // Escrever rodapé
    time_t now = time(NULL);
    char footer[256];
    snprintf(footer, sizeof(footer), "\n================================\nLog terminado em: %s", ctime(&now));
    
    if (log_current_pos + strlen(footer) < LOG_FILE_SIZE) {
        memcpy(log_buffer + log_current_pos, footer, strlen(footer));
        log_current_pos += strlen(footer);
    }
    
    // Sincronizar buffer com disco
    msync(log_buffer, log_current_pos, MS_SYNC);
    
    // Ajustar tamanho real do ficheiro
    if (log_fd != -1) {
        ftruncate(log_fd, log_current_pos);
    }
    
    // Desmapear memória
    munmap(log_buffer, LOG_FILE_SIZE);
    log_buffer = NULL;
    
    // Fechar ficheiro
    if (log_fd != -1) {
        close(log_fd);
        log_fd = -1;
    }
    
    pthread_mutex_unlock(&log_mutex);
    
    #ifdef DEBUG
    printf("[DEBUG] Ficheiro de log fechado\n");
    printf("[DEBUG] Total de bytes escritos: %zu\n", log_current_pos);
    #endif
    
    printf("\nLog guardado em: %s (%zu bytes)\n", LOG_FILENAME, log_current_pos);
}