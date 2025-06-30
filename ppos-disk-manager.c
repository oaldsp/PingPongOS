#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "ppos-disk-manager.h"
#include "disk-driver.h"
#include "queue.h"
#include "ppos-core-globals.h"

// Variáveis estáticas do gerente de disco
static task_t disk_task;
static diskrequest_t *request_queue = NULL;
static semaphore_t sem_disk;
static semaphore_t sem_requests;
static int initialized = 0;
static int block_size_global = 0;
static int num_blocks_global = 0;

// Protótipos de funções locais
void disk_driver(void *arg);
void disk_signal_handler(int signum);

// Inicializa o gerente de disco
int disk_mgr_init(int *num_blocks, int *block_size) {
    if (initialized) return -1;

    if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0) {
        perror("disk_mgr_init: Falha ao inicializar o disco");
        return -1;
    }

    *num_blocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    if (*num_blocks < 0) {
        perror("disk_mgr_init: Falha ao obter o número de blocos");
        return -1;
    }

    *block_size = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);
    if (*block_size < 0) {
        perror("disk_mgr_init: Falha ao obter o tamanho do bloco");
        return -1;
    }

    num_blocks_global = *num_blocks;
    block_size_global = *block_size;

    if (sem_create(&sem_disk, 0) != 0) return -1;
    if (sem_create(&sem_requests, 0) != 0) return -1;
    if (task_create(&disk_task, disk_driver, NULL) < 0) return -1;

    // A solução correta: prioridade alta para o driver.
    task_setprio(&disk_task, -20);

    // Registra o tratador de sinal para a interrupção do disco
    signal(SIGUSR1, disk_signal_handler);

    initialized = 1;
    return 0;
}

// Solicita a leitura de um bloco do disco
int disk_block_read(int block, void *buffer) {
    if (!initialized || block < 0 || block >= num_blocks_global)
        return -1;

    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    if (!req) return -1;

    req->task = taskExec;
    req->block = block;
    req->buffer = buffer;
    req->operation = 0;
    req->status = 0;
    req->next = NULL;

    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests);

    while (req->status == 0) {
        task_yield();
    }

    int result = (req->status == 1) ? 0 : -1;
    free(req);
    return result;
}

// Solicita a escrita de um bloco no disco
int disk_block_write(int block, void *buffer) {
    if (!initialized || block < 0 || block >= num_blocks_global)
        return -1;

    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    if (!req) return -1;

    req->task = taskExec;
    req->block = block;
    req->buffer = buffer;
    req->operation = 1;
    req->status = 0;
    req->next = NULL;

    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests);

    while (req->status == 0) {
        task_yield();
    }

    int result = (req->status == 1) ? 0 : -1;
    free(req);
    return result;
}

// Tratador de sinal para a interrupção de hardware do disco (SIGUSR1)
void disk_signal_handler(int signum) {
    if (signum == SIGUSR1) {
        // --- CORREÇÃO AQUI ---
        // Ação MÍNIMA e SEGURA: apenas sinalizar o semáforo.
        // NUNCA chame task_yield() ou swapcontext() de um tratador de sinal.
        sem_up(&sem_disk);
    }
}

// Tarefa do driver de disco (executa em loop infinito)
void disk_driver(void *arg) {
    (void) arg;

    while (1) {
        
        // Aguarda por uma requisição
        sem_down(&sem_requests);

        // Pega a requisição da fila
        diskrequest_t *req = (diskrequest_t*) queue_remove((queue_t**)&request_queue, (queue_t*)request_queue);
        if (!req) continue;

        // Verifica se é uma requisição "poison pill" para terminar
        if (req->block == -1) {
            free(req);    // Libera a requisição venenosa
            break;        // Sai do laço while(1)
        }
        
        // Lógica normal de processamento
        int cmd = (req->operation == 0) ? DISK_CMD_READ : DISK_CMD_WRITE;
        int op_status = disk_cmd(cmd, req->block, req->buffer);
        sem_down(&sem_disk);

        if (op_status < 0)
            req->status = -1;
        else
            req->status = 1;
    }

    // A tarefa do driver termina a si mesma
    task_exit(0);
}

