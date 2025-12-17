/*
 * Sistemas Operativos 2025/2026
 * Projeto: Urgências@DEI
 * 
 * Aluno : Diogo Marques de Lemos - 2020219666
 */

#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include "config.h"
#include "doctor.h"
#include "shm.h"
#include "pipe.h"
#include "patient.h"
#include "msq.h"
#include "triage.h"
#include "log.h"

#define DEBUG 

/* Variável global para a configuração */
Config global_config;

/* Flag para terminação controlada */
volatile sig_atomic_t keep_running = 1;

/* Contador de pacientes */
int patient_counter = 0;

/* File descriptor do named pipe */
int pipe_fd = -1;

/*
 * Bloqueia sinais indesejados 
 */
void block_unwanted_signals() {
    sigset_t block_set;
    
    // Inicializar conjunto vazio
    sigemptyset(&block_set);
    
    // Adicionar sinais a bloquear  
    sigaddset(&block_set, SIGHUP);    // Ignorar desconexão de terminal
    sigaddset(&block_set, SIGQUIT);   // Ignorar SIGQUIT (Ctrl+\)
    sigaddset(&block_set, SIGTSTP);   // Ignorar Ctrl+Z
    sigaddset(&block_set, SIGTTIN);   // Ignorar leitura em background
    sigaddset(&block_set, SIGTTOU);   // Ignorar escrita em background
    sigaddset(&block_set, SIGPIPE);   // Ignorar pipe quebrado
    
    // Bloquear estes sinais
    if (pthread_sigmask(SIG_BLOCK, &block_set, NULL) != 0) {
        perror("Erro ao bloquear sinais indesejados");
        write_log("AVISO: Falha ao bloquear sinais indesejados");
    } else {
        write_log("Sinais indesejados bloqueados com sucesso");
        #ifdef DEBUG
        write_log("DEBUG: SIGTERM, SIGHUP, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGPIPE bloqueados");
        #endif
    }
}

/* Handler para SIGINT */
void sigint_handler(int signum) {
    (void)signum;
    write_log("SINAL: SIGINT recebido - Iniciando terminação controlada");
    keep_running = 0;
}

/* Handler para SIGUSR1 */
void sigusr1_handler(int signum) {
    (void)signum;
    // Guardar errno para não interferir com syscalls
    int saved_errno = errno;
    
    write_log("SINAL: SIGUSR1 recebido - Estatísticas solicitadas");
    
    // Ler e apresentar estatísticas da memória partilhada
    print_statistics();
    
    // Mostrar também o estado das filas
    int queue_size = get_queue_size();
    if (queue_size >= 0) {
        printf("Pacientes na fila de atendimento: %d\n", queue_size);
        write_log("Fila de atendimento: %d pacientes", queue_size);
    }
    
    if (triage_queue != NULL) {
        pthread_mutex_lock(&triage_queue->mutex);
        printf("Pacientes na fila de triagem: %d/%d\n\n", 
               triage_queue->count, triage_queue->capacity);
        write_log("Fila de triagem: %d/%d pacientes", 
                  triage_queue->count, triage_queue->capacity);
        pthread_mutex_unlock(&triage_queue->mutex);
    }
    
    // Restaurar errno
    errno = saved_errno;
}

/* Handler para SIGCHLD - detetar quando um Doctor termina */
void sigchld_handler(int signum) {
    (void)signum;
    // Guardar errno
    int saved_errno = errno;
    
    pid_t pid;
    int status;
    
    // Recolher todos os processos filhos que terminaram
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int found = 0;
        
        // Verificar se é um doctor permanente
        if (doctors_array != NULL) {
            for (int i = 0; i < global_config.doctors; i++) {
                if (doctors_array[i].pid == pid) {
                    write_log("Doctor %d (PID: %d) terminou o turno", 
                             doctors_array[i].id, pid);
                    
                    // Se o sistema ainda está a correr, criar novo Doctor
                    if (keep_running) {
                        write_log("A criar novo Doctor %d para substituir...", 
                                 doctors_array[i].id);
                        create_doctor_process(doctors_array[i].id, &global_config);
                    }
                    found = 1;
                    break;
                }
            }
        }      
        // Se não foi encontrado, é um doctor temporário
        if (!found && num_temporary_doctors > 0) {
            num_temporary_doctors--;
            write_log("Doctor temporário (PID: %d) terminou. Temporários restantes: %d", 
                     pid, num_temporary_doctors);
        }
    }
    // Restaurar errno
    errno = saved_errno;
}

/*
 * Configura os handlers de sinais
 */
void setup_signal_handlers() {
    struct sigaction sa_int, sa_usr1, sa_chld;
    
    // Bloquear sinais indesejados
    block_unwanted_signals();
    
    // Configurar handler para SIGINT
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART; // Reiniciar syscalls interrompidas
    
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("Erro ao configurar handler SIGINT");
        exit(EXIT_FAILURE);
    }
    
    // Configurar handler para SIGUSR1
    sa_usr1.sa_handler = sigusr1_handler;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = SA_RESTART; // Reiniciar syscalls interrompidas
    
    if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
        perror("Erro ao configurar handler SIGUSR1");
        exit(EXIT_FAILURE);
    }
    
    // Configurar handler para SIGCHLD
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; // Reiniciar syscalls + ignorar STOP
    
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("Erro ao configurar handler SIGCHLD");
        exit(EXIT_FAILURE);
    }
    
    // Ignorar SIGPIPE explicitamente (caso não tenha sido bloqueado)
    signal(SIGPIPE, SIG_IGN);
    
    write_log("Handlers de sinais configurados");
    write_log("  - SIGINT: Terminação controlada");
    write_log("  - SIGUSR1: Mostrar estatísticas");
    write_log("  - SIGCHLD: Monitorizar processos filhos");
}

/*
 * Processa uma linha recebida do named pipe
 * Formato: "João 10 50 1" ou "8 10 65 3" ou "TRIAGE=10"
 */
void process_pipe_input(const char *line) {
    char name[MAX_NAME_LENGTH];
    int triage_time, attendance_time, priority;
    int count;
    int new_triage_value;
    
    // Ignorar linhas vazias
    if (strlen(line) == 0) {
        return;
    }
    
    #ifdef DEBUG
    write_log("DEBUG: A processar linha: %s", line);
    #endif
    
    // Verificar se é um comando TRIAGE=X
    if (sscanf(line, "TRIAGE=%d", &new_triage_value) == 1 ||
        sscanf(line, "TRIAGE = %d", &new_triage_value) == 1) {
        
        write_log("COMANDO: Alteração de threads de triagem solicitada (TRIAGE=%d)", 
                 new_triage_value);
        
        // Validar valor
        if (new_triage_value <= 0 || new_triage_value > 100) {
            write_log("ERRO: Valor inválido para TRIAGE (%d). Deve estar entre 1 e 100.", 
                     new_triage_value);
            return;
        }
        
        // Atualizar configuração global
        global_config.triage = new_triage_value;
        
        // Aplicar alteração
        if (change_triage_threads(new_triage_value, &global_config) == 0) {
            write_log("Configuração TRIAGE atualizada para %d threads", new_triage_value);
        } else {
            write_log("ERRO: Falha ao alterar número de threads de triagem");
        }
        
        return;
    }
    
    // Verificar se é um paciente individual ou grupo
    if (isdigit(line[0])) {
        // Grupo de pacientes: "8 10 65 3"
        if (sscanf(line, "%d %d %d %d", &count, &triage_time, &attendance_time, &priority) == 4) {
            // Validar valores
            if (count <= 0 || count > 1000) {
                write_log("ERRO: Número de pacientes inválido (%d). Deve estar entre 1 e 1000", count);
                return;
            }
            
            if (triage_time <= 0 || triage_time > 10000) {
                write_log("ERRO: Tempo de triagem inválido (%d ms). Deve estar entre 1 e 10000", triage_time);
                return;
            }
            
            if (attendance_time <= 0 || attendance_time > 100000) {
                write_log("ERRO: Tempo de atendimento inválido (%d ms). Deve estar entre 1 e 100000", attendance_time);
                return;
            }
            
            if (priority < 1 || priority > 5) {
                write_log("ERRO: Prioridade inválida (%d). Deve estar entre 1 (mais urgente) e 5 (menos urgente)", priority);
                return;
            }
            
            write_log("RECEÇÃO: Grupo de %d pacientes (triagem=%dms, atend=%dms, prior=%d)",
                     count, triage_time, attendance_time, priority);
            
            int success_count = 0;
            int failed_count = 0;
            
            for (int i = 0; i < count; i++) {
                patient_counter++;
                
                // Gerar nome automático
                time_t now = time(NULL);
                struct tm *tm_info = localtime(&now);
                snprintf(name, MAX_NAME_LENGTH, "%04d%02d%02d-%03d",
                        tm_info->tm_year + 1900, tm_info->tm_mon + 1, 
                        tm_info->tm_mday, patient_counter);
                
                Patient *patient = create_patient(patient_counter, name, 
                                                 triage_time, attendance_time, priority);
                
                if (patient != NULL) {
                    write_log("PACIENTE: %s criado (Nº %d)", name, patient_counter);
                    
                    // Adicionar à fila de triagem
                    if (enqueue_patient(triage_queue, patient) != 0) {
                        write_log("ERRO: Paciente %s descartado (fila de triagem cheia)", name);
                        free_patient(patient);
                        failed_count++;
                    } else {
                        write_log("TRIAGEM: Paciente %s adicionado à fila", name);
                        success_count++;
                    }
                } else {
                    write_log("ERRO: Falha ao criar paciente");
                    failed_count++;
                }
            }
            
            write_log("RESUMO: %d pacientes adicionados, %d descartados", success_count, failed_count);
            
        } else {
            write_log("ERRO: Formato inválido para grupo de pacientes. Formato esperado: 'N triage atend prior'");
            write_log("      Exemplo: '5 20 100 2' (5 pacientes, 20ms triagem, 100ms atend, prioridade 2)");
        }
    } else {
        // Paciente individual: "João 10 50 1"
        if (sscanf(line, "%63s %d %d %d", name, &triage_time, &attendance_time, &priority) == 4) {
            // Validar valores
            if (strlen(name) == 0 || strlen(name) >= MAX_NAME_LENGTH) {
                write_log("ERRO: Nome inválido (tamanho: %zu). Deve ter entre 1 e %d caracteres", 
                         strlen(name), MAX_NAME_LENGTH - 1);
                return;
            }
            
            if (triage_time <= 0 || triage_time > 10000) {
                write_log("ERRO: Tempo de triagem inválido (%d ms). Deve estar entre 1 e 10000", triage_time);
                return;
            }
            
            if (attendance_time <= 0 || attendance_time > 100000) {
                write_log("ERRO: Tempo de atendimento inválido (%d ms). Deve estar entre 1 e 100000", attendance_time);
                return;
            }
            
            if (priority < 1 || priority > 5) {
                write_log("ERRO: Prioridade inválida (%d). Deve estar entre 1 (mais urgente) e 5 (menos urgente)", priority);
                return;
            }
            
            patient_counter++;
            
            write_log("RECEÇÃO: Paciente '%s' (triagem=%dms, atend=%dms, prior=%d)",
                     name, triage_time, attendance_time, priority);
            
            Patient *patient = create_patient(patient_counter, name, 
                                             triage_time, attendance_time, priority);
            
            if (patient != NULL) {
                write_log("PACIENTE: %s criado (Nº %d)", name, patient_counter);
                
                // Adicionar à fila de triagem
                if (enqueue_patient(triage_queue, patient) != 0) {
                    write_log("ERRO: Paciente %s descartado (fila de triagem cheia)", name);
                    free_patient(patient);
                } else {
                    write_log("TRIAGEM: Paciente %s adicionado à fila", name);
                }
            } else {
                write_log("ERRO: Falha ao criar paciente %s", name);
            }
        } else {
            write_log("ERRO: Formato inválido. Formatos esperados:");
            write_log("      Paciente: 'Nome triage atend prior' (ex: 'João 15 60 1')");
            write_log("      Grupo: 'N triage atend prior' (ex: '5 20 100 2')");
            write_log("      Comando: 'TRIAGE=N' (ex: 'TRIAGE=10')");
        }
    }
}

/*
 * Lê dados do named pipe (não-bloqueante)
 */
void read_from_pipe() {
    static char buffer[PIPE_BUFFER_SIZE];
    static int buffer_pos = 0;
    
    char temp[PIPE_BUFFER_SIZE];
    ssize_t bytes_read = read(pipe_fd, temp, sizeof(temp) - 1);
    
    if (bytes_read > 0) {
        temp[bytes_read] = '\0';
        
        // Adicionar ao buffer
        for (int i = 0; i < bytes_read; i++) {
            if (temp[i] == '\n' || temp[i] == '\r') {
                if (buffer_pos > 0) {
                    buffer[buffer_pos] = '\0';
                    process_pipe_input(buffer);
                    buffer_pos = 0;
                }
            } else {
                if (buffer_pos < PIPE_BUFFER_SIZE - 1) {
                    buffer[buffer_pos++] = temp[i];
                }
            }
        }
    } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        write_log("ERRO: Falha ao ler do named pipe");
    }
}

/*
 * Função principal do processo Admission
 */
int main(void) {
    printf("=== Urgências@DEI - Sistema de Simulação ===\n");
    printf("Iniciando processo Admission (PID: %d)...\n", getpid());
    
    // Registar hora de início
    time_t start_time = time(NULL);
    
    // 1. Criar ficheiro de log mapeado em memória 
    if (create_log_file() != 0) {
        fprintf(stderr, "ERRO: Falha ao criar ficheiro de log\n");
        return EXIT_FAILURE;
    }
    
    write_log("=== INÍCIO DO PROGRAMA ===");
    write_log("Processo Admission iniciado (PID: %d)", getpid());
    
    // 2. Carregar configuração
    write_log("A carregar configuração...");
    
    if (load_config("config.txt", &global_config) != 0) {
        write_log("ERRO: Falha ao carregar configuração");
        close_log_file();
        return EXIT_FAILURE;
    }
    
    write_log("Configuração carregada com sucesso");
    write_log("TRIAGE_QUEUE_MAX: %d", global_config.triage_queue_max);
    write_log("TRIAGE: %d", global_config.triage);
    write_log("DOCTORS: %d", global_config.doctors);
    write_log("SHIFT_LENGTH: %d segundos", global_config.shift_length);
    write_log("MSQ_WAIT_MAX: %d", global_config.msq_wait_max);
    
    print_config(&global_config);
    
    // 3. Configurar handlers de sinais
    setup_signal_handlers();
    
    // 4. Criar memória partilhada
    write_log("A criar memória partilhada...");
    
    if (create_shared_memory() != 0) {
        write_log("ERRO: Falha ao criar memória partilhada");
        close_log_file();
        return EXIT_FAILURE;
    }
    
    write_log("Memória partilhada criada com sucesso");
    
    // 5. Criar named pipe
    write_log("A criar named pipe...");
    
    if (create_named_pipe() != 0) {
        write_log("ERRO: Falha ao criar named pipe");
        destroy_shared_memory();
        close_log_file();
        return EXIT_FAILURE;
    }
    
    pipe_fd = open_named_pipe_read();
    if (pipe_fd == -1) {
        write_log("ERRO: Falha ao abrir named pipe");
        destroy_named_pipe();
        destroy_shared_memory();
        close_log_file();
        return EXIT_FAILURE;
    }
    
    write_log("Named pipe criado e aberto com sucesso");
    
    // 6. Criar fila de mensagens
    write_log("A criar fila de mensagens...");
    
    if (create_message_queue() != 0) {
        write_log("ERRO: Falha ao criar fila de mensagens");
        close_named_pipe(pipe_fd);
        destroy_named_pipe();
        destroy_shared_memory();
        close_log_file();
        return EXIT_FAILURE;
    }
    
    write_log("Fila de mensagens criada com sucesso (ID: %d)", msq_id);
    
    // 7. Criar threads de triagem
    write_log("A criar %d threads de triagem...", global_config.triage);
    
    if (create_triage_threads(global_config.triage, &global_config) != 0) {
        write_log("ERRO: Falha ao criar threads de triagem");
        destroy_message_queue();
        close_named_pipe(pipe_fd);
        destroy_named_pipe();
        destroy_shared_memory();
        close_log_file();
        return EXIT_FAILURE;
    }
    
    write_log("%d threads de triagem criadas com sucesso", global_config.triage);
    
    // 8. Criar processos Doctor
    write_log("A criar %d processos Doctor...", global_config.doctors);
    
    if (create_all_doctors(&global_config) != 0) {
        write_log("ERRO: Falha ao criar processos Doctor");
        terminate_triage_threads();
        destroy_message_queue();
        close_named_pipe(pipe_fd);
        destroy_named_pipe();
        destroy_shared_memory();
        close_log_file();
        return EXIT_FAILURE;
    }
    
    write_log("%d processos Doctor criados com sucesso", global_config.doctors);
    
    // 9. Loop principal
    write_log("=== SISTEMA PRONTO ===");
    write_log("A aguardar pacientes...");
    
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║           Sistema pronto! A aguardar pacientes...         ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║ Para enviar pacientes, use:                               ║\n");
    printf("║   echo \"João 10 50 1\" > input_pipe                        ║\n");
    printf("║   echo \"5 20 100 2\" > input_pipe                          ║\n");
    printf("║                                                            ║\n");
    printf("║ Comandos:                                                  ║\n");
    printf("║   kill -SIGUSR1 %d  -> Ver estatísticas               ║\n", getpid());
    printf("║   Ctrl+C              -> Terminar sistema                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Loop principal com select para leitura não-bloqueante
    fd_set read_fds;
    struct timeval timeout;
    int check_counter = 0; // Contador para verificar fila periodicamente
    
    while (keep_running) {
        FD_ZERO(&read_fds);
        FD_SET(pipe_fd, &read_fds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ready = select(pipe_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready > 0 && FD_ISSET(pipe_fd, &read_fds)) {
            read_from_pipe();
        } else if (ready == -1 && errno != EINTR) {
            write_log("ERRO: Falha no select");
            break;
        }
        // Verificar periodicamente se é necessário criar doctor temporário
        check_counter++;
        if (check_counter >= 5) { // A cada 5 segundos
            check_and_create_temporary_doctor(&global_config);
            check_counter = 0;
        }
    }
    
    // 10. Terminação controlada
    write_log("=== TERMINAÇÃO CONTROLADA ===");
    
    // Mostrar estatísticas finais
    printf("\n=== ESTATÍSTICAS FINAIS ===\n");
    write_log("Estatísticas finais:");
    print_statistics();
    
    int queue_size = get_queue_size();
    if (queue_size >= 0) {
        write_log("Pacientes na fila de atendimento: %d", queue_size);
    }
    
    // Terminar threads de triagem
    write_log("A terminar threads de triagem...");
    terminate_triage_threads();
    
    // Terminar todos os processos Doctor
    write_log("A terminar processos Doctor...");
    terminate_all_doctors();
    
    // Destruir fila de mensagens
    write_log("A destruir fila de mensagens...");
    destroy_message_queue();
    
    // Fechar e destruir named pipe
    write_log("A fechar named pipe...");
    close_named_pipe(pipe_fd);
    destroy_named_pipe();
    
    // Destruir memória partilhada
    write_log("A destruir memória partilhada...");
    destroy_shared_memory();
    
    time_t end_time = time(NULL);
    write_log("=== FIM DO PROGRAMA ===");
    write_log("Tempo total de execução: %.0f segundos", difftime(end_time, start_time));
    
    // Fechar ficheiro de log (deve ser o último!)
    close_log_file();
    
    return EXIT_SUCCESS;
}