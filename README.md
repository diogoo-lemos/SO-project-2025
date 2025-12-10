# Arquitetura do Sistema Urgências@DEI
**Sistemas Operativos 2025/2026**

## 1. Visão Geral da Arquitetura
```
┌─────────────────────────────────────────────────────────────────────┐
│                        PROCESSO ADMISSION                           │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │                    Thread Principal                            │ │
│  │  - Lê configurações (config.txt)                              │ │
│  │  - Gere recursos IPC                                          │ │
│  │  - Monitoriza processos filhos (SIGCHLD)                      │ │
│  │  - Recebe comandos (SIGINT, SIGUSR1)                          │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                              ↓                                       │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │               Pool de Threads de Triagem                       │ │
│  │  Thread 1 │ Thread 2 │ ... │ Thread T                         │ │
│  │  (Dinâmico: pode aumentar/diminuir via TRIAGE=X)              │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
                              ↓ ↓ ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     MECANISMOS IPC                                  │
│  ┌────────────┐  ┌──────────┐  ┌─────────┐  ┌──────────────────┐  │
│  │ Named Pipe │  │   MSQ    │  │   SHM   │  │  MMF (Log File)  │  │
│  │input_pipe  │  │(Msg Queue)│ │(Statistics)│ DEI_Emergency.log│  │
│  └────────────┘  └──────────┘  └─────────┘  └──────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                              ↓ ↓ ↓
┌─────────────────────────────────────────────────────────────────────┐
│                      PROCESSOS DOCTOR                               │
│  ┌──────────┐  ┌──────────┐        ┌──────────┐  ┌──────────────┐ │
│  │Doctor 1  │  │Doctor 2  │  ...   │Doctor D  │  │Doctor TEMP-1 │ │
│  │(Turno 5s)│  │(Turno 5s)│        │(Turno 5s)│  │(Sob demanda) │ │
│  └──────────┘  └──────────┘        └──────────┘  └──────────────┘ │
│      ↓              ↓                    ↓              ↓           │
│  [Substitui]   [Substitui]          [Substitui]   [Termina quando  │
│  ao terminar   ao terminar          ao terminar    fila < 80%]     │
└─────────────────────────────────────────────────────────────────────┘
```

## 2. Fluxo de Dados
```
1. ENTRADA DE PACIENTES
   ┌─────────────┐
   │ Utilizador  │
   └──────┬──────┘
          │ echo "João 10 50 1" > input_pipe
          ↓
   ┌─────────────────┐
   │   Named Pipe    │ (FIFO)
   │   input_pipe    │
   └────────┬────────┘
            │
            ↓
   ┌─────────────────────────┐
   │  Admission: read_from_  │
   │  pipe() / process_pipe_ │
   │  input()                │
   └────────┬────────────────┘
            │
            ↓
   ┌─────────────────────────┐
   │  Criar Patient struct   │
   │  clock_gettime(arrival) │
   └────────┬────────────────┘
            │
            ↓
   ┌─────────────────────────┐
   │  enqueue_patient()      │
   │  → triage_queue         │
   └─────────────────────────┘

2. TRIAGEM
   ┌─────────────────────────┐
   │   triage_queue (FIFO)   │
   │   [Mutex + Cond Vars]   │
   └────────┬────────────────┘
            │ dequeue_patient()
            ↓
   ┌─────────────────────────┐
   │ Thread de Triagem 1..T  │
   │ - clock_gettime(start)  │
   │ - usleep(triage_time)   │
   │ - clock_gettime(end)    │
   └────────┬────────────────┘
            │
            ├─→ update_triaged_stats() → SHM [Mutex]
            │
            ↓
   ┌─────────────────────────┐
   │ send_patient_to_queue() │
   │ mtype = priority (1-5)  │
   └────────┬────────────────┘
            │
            ↓
   ┌─────────────────────────┐
   │ MSQ (Message Queue)     │
   │ Priorização por mtype   │
   └─────────────────────────┘

3. ATENDIMENTO
   ┌─────────────────────────┐
   │ MSQ (Message Queue)     │
   │ msgrcv(..., -5, ...)    │
   └────────┬────────────────┘
            │ receive_patient_from_queue()
            │ (prioridade 1 atendida primeiro)
            ↓
   ┌─────────────────────────┐
   │ Processo Doctor 1..D    │
   │ - clock_gettime(start)  │
   │ - usleep(attend_time)   │
   │ - clock_gettime(end)    │
   └────────┬────────────────┘
            │
            ↓
   ┌─────────────────────────┐
   │update_attended_stats()  │
   │ → SHM [Mutex]           │
   └─────────────────────────┘
```

## 3. Mecanismos de Sincronização

### 3.1. Mutexes

| Mutex | Localização | Tipo | Propósito |
|-------|-------------|------|-----------|
| `triage_queue->mutex` | triage.c | PTHREAD | Sincroniza acesso à fila de triagem (threads) |
| `shm_stats->mutex` | shm.c | PTHREAD_PROCESS_SHARED | Sincroniza acesso às estatísticas (threads + processos) |
| `log_mutex` | log.c | PTHREAD | Sincroniza escrita no log (threads) |
| `triage_control_mutex` | triage.c | PTHREAD | Sincroniza alteração dinâmica de threads |

### 3.2. Variáveis de Condição

| Cond Var | Associada a | Propósito |
|----------|-------------|-----------|
| `triage_queue->not_empty` | `triage_queue->mutex` | Sinaliza que há pacientes para triar |
| `triage_queue->not_full` | `triage_queue->mutex` | Sinaliza que há espaço na fila |

### 3.3. Fila de Mensagens (MSQ)
```c
// Tipo: System V Message Queue
// Chave: ftok("/tmp", 'U')
// Priorização: mtype = priority (1-5)
// Operações:
//   - msgsnd(..., IPC_NOWAIT)     // Envio não-bloqueante
//   - msgrcv(..., -5, IPC_NOWAIT) // Recebe menor mtype (maior prioridade)
```

### 3.4. Named Pipe (FIFO)
```c
// Tipo: mkfifo()
// Nome: "input_pipe"
// Modo: O_RDONLY | O_NONBLOCK (leitura não-bloqueante)
// Uso: Receber pacientes e comandos (ex: TRIAGE=10)
```

### 3.5. Memória Partilhada (SHM)
```c
// Tipo: POSIX Shared Memory (shm_open + mmap)
// Nome: "/urgencias_shm"
// Tamanho: sizeof(Statistics)
// Sincronização: pthread_mutex com PTHREAD_PROCESS_SHARED
// Conteúdo:
//   - total_triaged (int)
//   - total_attended (int)
//   - total_wait_triage (double)
//   - total_wait_doctor (double)
//   - total_time_system (double)
```

### 3.6. Memory-Mapped File (MMF)
```c
// Tipo: mmap()
// Ficheiro: "DEI_Emergency.log"
// Tamanho: 10 MB
// Modo: MAP_SHARED
// Sincronização: pthread_mutex (log_mutex)
// Persistência: msync(MS_SYNC)
```

## 4. Gestão de Sinais

### 4.1. Processo Admission

| Sinal | Handler | Ação |
|-------|---------|------|
| SIGINT | `sigint_handler()` | Terminação controlada (keep_running = 0) |
| SIGUSR1 | `sigusr1_handler()` | Imprime estatísticas |
| SIGCHLD | `sigchld_handler()` | Deteta fim de Doctor e cria substituto |

**Sinais Bloqueados:** SIGTERM, SIGHUP, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGPIPE

### 4.2. Processos Doctor

| Sinal | Handler | Ação |
|-------|---------|------|
| SIGALRM | `sigalrm_handler()` | Fim do turno (shift_active = 0) |
| SIGTERM | `sigalrm_handler()` | Terminação pelo pai |

**Sinais Bloqueados:** SIGINT, SIGUSR1, SIGHUP, SIGQUIT, SIGTSTP, SIGPIPE

## 5. Concorrência e Paralelismo

### 5.1. Threads de Triagem
- **Número:** Configurável (TRIAGE em config.txt)
- **Dinâmico:** Pode ser alterado em runtime (TRIAGE=X)
- **Sincronização:** Mutex + Variáveis de Condição
- **Partilha:** triage_queue (fila circular thread-safe)

### 5.2. Processos Doctor
- **Número fixo:** DOCTORS em config.txt
- **Número temporário:** Dinâmico (quando MSQ >= MSQ_WAIT_MAX)
- **Sincronização:** MSQ (priorização) + SHM (estatísticas)
- **Substituição:** Automática após SHIFT_LENGTH segundos

## 6. Tratamento de Erros

### 6.1. Erros de Criação
- Falha em criar recursos IPC → Log + exit()
- Falha em fork/pthread_create → Log + retry ou abort

### 6.2. Erros de Operação
- Fila de triagem cheia → Paciente descartado + log
- Fila de mensagens cheia → Erro retornado + log
- Falha em locks → Erro retornado + log

### 6.3. Validações
- Valores de entrada (nome, tempos, prioridade)
- Comandos (TRIAGE=X: 1-100)
- Recursos (verificação de NULL, -1)

## 7. Decisões de Design

### 7.1. Fila Circular para Triagem
**Razão:** Eficiência em FIFO com tamanho fixo

### 7.2. Prioridade via mtype na MSQ
**Razão:** Kernel faz priorização automaticamente

### 7.3. Log Mapeado em Memória
**Razão:** Performance (evita syscalls frequentes)

### 7.4. Mutex PROCESS_SHARED na SHM
**Razão:** Sincronizar threads E processos

### 7.5. Doctors Temporários
**Razão:** Escalabilidade automática sob carga

## 8. Estatísticas Calculadas
```
Tempo de espera antes da triagem = triage_start - arrival_time
Tempo de espera antes do atendimento = attendance_start - triage_end
Tempo total no sistema = attendance_end - arrival_time

Médias = Σ(tempos) / total_pacientes
```

## 9. Limitações e Restrições

- **TRIAGE_QUEUE_MAX:** Limite de pacientes aguardando triagem
- **MSQ_WAIT_MAX:** Limite de pacientes aguardando atendimento
- **SHIFT_LENGTH:** Duração fixa dos turnos
- **Log File:** 10 MB (sem remapeamento)
- **Threads de Triagem:** 1-100
- **Prioridade:** 1 (urgente) a 5 (não urgente)
