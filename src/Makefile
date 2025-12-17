# Makefile para o projeto Urgências@DEI
# Sistemas Operativos 2025/2026

CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -pthread -lrt

# Ficheiros objeto
OBJ = admission.o config.o doctor.o shm.o pipe.o patient.o msq.o triage.o log.o

# Executável principal
TARGET = admission

# Regra principal
all: $(TARGET)

# Compilar o executável
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

# Compilar ficheiros objeto
admission.o: admission.c config.h doctor.h shm.h pipe.h patient.h msq.h triage.h log.h
	$(CC) $(CFLAGS) -c admission.c

config.o: config.c config.h
	$(CC) $(CFLAGS) -c config.c

doctor.o: doctor.c doctor.h config.h shm.h msq.h log.h
	$(CC) $(CFLAGS) -c doctor.c

shm.o: shm.c shm.h
	$(CC) $(CFLAGS) -c shm.c

pipe.o: pipe.c pipe.h
	$(CC) $(CFLAGS) -c pipe.c

patient.o: patient.c patient.h
	$(CC) $(CFLAGS) -c patient.c

msq.o: msq.c msq.h patient.h
	$(CC) $(CFLAGS) -c msq.c

triage.o: triage.c triage.h config.h patient.h shm.h msq.h log.h
	$(CC) $(CFLAGS) -c triage.c

log.o: log.c log.h
	$(CC) $(CFLAGS) -c log.c

# Limpar ficheiros compilados
clean:
	rm -f $(OBJ) $(TARGET)
	rm -f DEI_Emergency.log
	rm -f input_pipe
	rm -f /dev/shm/urgencias_shm
	ipcrm -a 2>/dev/null || true

# Executar o programa
run: $(TARGET)
	./$(TARGET)

# Regra para debug
debug: CFLAGS += -DDEBUG
debug: clean all

# Limpar recursos IPC manualmente
clean-ipc:
	ipcrm -a 2>/dev/null || true
	rm -f /dev/shm/urgencias_shm

.PHONY: all clean run debug clean-ipc