#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "ppos-disk-manager.h"
#include "disk-driver.h"
#include "queue.h"
#include "ppos-core-globals.h"

// Variáveis globais estáticas (sem mudanças)
volatile sig_atomic_t g_disk_interrupt_flag = 0;
static semaphore_t sem_requests;
static task_t disk_task;
static diskrequest_t *request_queue = NULL;
static int initialized = 0;
static int block_size_global = 0;
static int num_blocks_global = 0;
static int current_block = 0;
static int blocks_moved = 0;

// --- FUNÇÃO DE ENCERRAMENTO (REGISTRADA COM atexit) ---
// Esta função será chamada automaticamente quando o programa principal terminar.
void disk_mgr_shutdown_handler() {
    if (!initialized) return;

    // Cria uma requisição "venenosa" para sinalizar o fim.
    diskrequest_t *poison_pill = malloc(sizeof(diskrequest_t));
    if (!poison_pill) return;

    poison_pill->block = -1; // Sinal especial de encerramento
    poison_pill->task = NULL;
    poison_pill->buffer = NULL;
    poison_pill->operation = -1;
    poison_pill->status = 0;
    poison_pill->next = NULL;

    // Adiciona a requisição na fila e acorda o driver para processá-la.
    queue_append((queue_t**)&request_queue, (queue_t*)poison_pill);
    sem_up(&sem_requests);

    // Espera a tarefa do driver terminar para garantir um desligamento limpo.
    task_join(&disk_task);

    // Imprime as estatísticas finais.
    printf("Gerente de disco encerrado via atexit.\n");
    printf("Estatísticas Finais -> Total de blocos percorridos: %d\n", blocks_moved);
    
    initialized = 0;
}


// --- DRIVER DE DISCO (COM LÓGICA DE ENCERRAMENTO) ---
void disk_driver(void *arg) {
    (void)arg;

    while (1) {
        sem_down(&sem_requests);

        // É mais seguro usar FCFS aqui para garantir que a "poison pill"
        // seja processada em ordem e não seja ignorada pelo SSTF.
        diskrequest_t *req = disk_scheduler_fcfs(); 
        // DISCO 1 -> _fcfs(765) _sstf(765) _cscan(765)
        // DISCO 2 -> _fcfs(7681) _sstf(5313) _cscan(11473)
        if (req) {
            queue_remove((queue_t**)&request_queue, (queue_t*)req);
        } else {
            continue;
        }

        // VERIFICA A REQUISIÇÃO DE ENCERRAMENTO
        if (req->block == -1) {
            free(req); // Libera a memória da requisição venenosa
            break;     // Sai do loop while(1)
        }

        // Lógica normal de processamento (igual a antes)
        int cmd = (req->operation == 0) ? DISK_CMD_READ : DISK_CMD_WRITE;
        disk_cmd(cmd, req->block, req->buffer);

        while (g_disk_interrupt_flag == 0) {
            task_yield();
        }
        g_disk_interrupt_flag = 0;
        
        req->status = 1;
        blocks_moved += abs(req->block - current_block);
        current_block = req->block;
        
        task_resume(req->task);
    }
    // A tarefa do driver termina a si mesma.
    task_exit(0);
}


// --- INICIALIZAÇÃO (COM REGISTRO DO atexit) ---
int disk_mgr_init(int *num_blocks, int *block_size) {
    if (initialized) return -1;

    if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0) return -1;
    *num_blocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    *block_size = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);
    if (*num_blocks < 0 || *block_size < 0) return -1;

    num_blocks_global = *num_blocks;
    block_size_global = *block_size;

    if (sem_create(&sem_requests, 0) != 0) return -1;
    if (task_create(&disk_task, disk_driver, NULL) < 0) return -1;
    task_setprio(&disk_task, -20);

    struct sigaction action;
    action.sa_handler = disk_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGUSR1, &action, 0) < 0) return -1;

    // ALTERAÇÃO CRÍTICA: Registra a função de limpeza para ser chamada no final.
    if (atexit(disk_mgr_shutdown_handler) != 0) {
        perror("Falha ao registrar a função de encerramento do disco");
        return -1;
    }
    
    initialized = 1;
    return 0;
}


// O restante do arquivo (funções de interface e escalonadores) permanece igual.
// --- FUNÇÕES DE INTERFACE PARA O USUÁRIO ---
int disk_block_read(int block, void *buffer) { /* ... código sem alterações ... */
    if (!initialized || block < 0 || block >= num_blocks_global) return -1;
    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    if (!req) return -1;
    req->task = taskExec; req->block = block; req->buffer = buffer; req->operation = 0; req->status = 0; req->next = NULL;
    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests);
    task_suspend(taskExec, NULL); task_yield();
    int result = (req->status == 1) ? 0 : -1; free(req); return result;
}
int disk_block_write(int block, void *buffer) { /* ... código sem alterações ... */
    if (!initialized || block < 0 || block >= num_blocks_global) return -1;
    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    if (!req) return -1;
    req->task = taskExec; req->block = block; req->buffer = buffer; req->operation = 1; req->status = 0; req->next = NULL;
    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests);
    task_suspend(taskExec, NULL); task_yield();
    int result = (req->status == 1) ? 0 : -1; free(req); return result;
}
void disk_signal_handler(int signum) { if (signum == SIGUSR1) { g_disk_interrupt_flag = 1; } }

// --- ESCALONADORES DE DISCO ---
diskrequest_t* disk_scheduler_fcfs() { return request_queue; }
diskrequest_t* disk_scheduler_sstf() { /* ... código sem alterações ... */
    if (!request_queue) return NULL;
    diskrequest_t *iter = request_queue, *closest = iter; int min_distance = abs(iter->block - current_block);
    iter = iter->next;
    while (iter != request_queue) {
        int dist = abs(iter->block - current_block);
        if (dist < min_distance) { closest = iter; min_distance = dist; }
        iter = iter->next;
    } return closest;
}
diskrequest_t* disk_scheduler_cscan() { /* ... código sem alterações ... */ 
    if (!request_queue) return NULL;
    diskrequest_t *iter = request_queue, *chosen = NULL, *first_in_line = NULL;
    int min_dist_fwd = INT_MAX, min_block_overall = INT_MAX;
    do {
        if (iter->block >= current_block) {
            int dist = iter->block - current_block;
            if (dist < min_dist_fwd) { min_dist_fwd = dist; chosen = iter; }
        }
        if (iter->block < min_block_overall) { min_block_overall = iter->block; first_in_line = iter; }
        iter = iter->next;
    } while (iter != request_queue);
    return chosen ? chosen : first_in_line;
}

