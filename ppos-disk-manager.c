#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h> // Para INT_MAX em C-SCAN

#include "ppos-disk-manager.h"
#include "disk-driver.h"
#include "queue.h"
#include "ppos-core-globals.h"

// Flag de comunicação com o signal handler. Essencial.
// 'volatile sig_atomic_t' garante que leituras e escritas sejam atômicas
// e que o compilador não otimize o acesso, fundamental para concorrência.
volatile sig_atomic_t g_disk_interrupt_flag = 0;

// Semáforo para controlar o fluxo de requisições.
// O driver dorme aqui quando não há NADA a fazer.
static semaphore_t sem_requests;

static task_t disk_task;
static diskrequest_t *request_queue = NULL;
static int initialized = 0;
static int block_size_global = 0;
static int num_blocks_global = 0;

static int current_block = 0; // Posição atual da cabeça do disco
static int blocks_moved = 0;  // Para estatísticas

// Tratador de sinal. Mínimo e seguro.
void disk_signal_handler(int signum) {
    if (signum == SIGUSR1) {
        // Ação mais segura possível em um handler: apenas setar uma flag.
        g_disk_interrupt_flag = 1;
    }
}

// Tarefa do driver de disco. Lógica revisada e correta.
void disk_driver(void *arg) {
    (void)arg;

    while (1) {
        // 1. Espera até que uma requisição chegue.
        // Se a fila de requisições estiver vazia, a tarefa bloqueia aqui.
        // Isso é seguro, pois significa que a 'main' ou outras tarefas de usuário
        // estão rodando e podem criar novas requisições.
        sem_down(&sem_requests);

        // 2. Acordou! Pega a próxima requisição da fila (usando um escalonador).
        diskrequest_t *req = disk_scheduler_cscan(); // Pode trocar para fcfs ou cscan aqui
        if (req) {
            queue_remove((queue_t**)&request_queue, (queue_t*)req);
        } else {
            continue; // Segurança, não deveria acontecer se sem_down retornou.
        }

        // 3. Envia o comando para o hardware do disco.
        int cmd = (req->operation == 0) ? DISK_CMD_READ : DISK_CMD_WRITE;
        disk_cmd(cmd, req->block, req->buffer);

        // 4. *** A SOLUÇÃO ***
        // Espera Ativa Cooperativa. O driver fica em um loop, cedendo
        // o processador, mas permanecendo na fila de PRONTOS.
        // Isso impede que a readyQueue fique vazia e o dispatcher entre em pânico.
        while (g_disk_interrupt_flag == 0) {
            task_yield();
        }

        // 5. O loop terminou, significa que a interrupção ocorreu.
        // Limpa a flag para a próxima operação.
        g_disk_interrupt_flag = 0;

        // A operação foi concluída com sucesso.
        req->status = 1;
        
        // Atualiza as estatísticas.
        blocks_moved += abs(req->block - current_block);
        current_block = req->block;

        // 6. Acorda a tarefa do usuário que estava suspensa esperando.
        task_resume(req->task);
    }
    task_exit(0);
}

// Funções de interface para o usuário (read/write)
int disk_block_read(int block, void *buffer) {
    if (!initialized || block < 0 || block >= num_blocks_global) return -1;
    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    if (!req) return -1;
    
    req->task = taskExec;
    req->block = block;
    req->buffer = buffer;
    req->operation = 0; // Leitura
    req->status = 0;
    req->next = NULL;

    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests); // Acorda o driver, se ele estiver dormindo

    // Suspende a tarefa atual e cede o processador.
    // Ela será acordada pelo driver quando a operação for concluída.
    task_suspend(taskExec, NULL);
    task_yield();

    // A execução retoma aqui. O resultado está em req->status.
    int result = (req->status == 1) ? 0 : -1;
    free(req);
    return result;
}

int disk_block_write(int block, void *buffer) {
    if (!initialized || block < 0 || block >= num_blocks_global) return -1;
    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    if (!req) return -1;
    
    req->task = taskExec;
    req->block = block;
    req->buffer = buffer;
    req->operation = 1; // Escrita
    req->status = 0;
    req->next = NULL;

    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests); // Acorda o driver

    task_suspend(taskExec, NULL);
    task_yield();

    int result = (req->status == 1) ? 0 : -1;
    free(req);
    return result;
}

// Função de inicialização do gerente de disco
int disk_mgr_init(int *num_blocks, int *block_size) {
    if (initialized) return -1;

    if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0) return -1;
    *num_blocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    *block_size = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);
    if (*num_blocks < 0 || *block_size < 0) return -1;

    num_blocks_global = *num_blocks;
    block_size_global = *block_size;

    // Semáforo de requisições começa em 0, para que o driver espere no início.
    if (sem_create(&sem_requests, 0) != 0) return -1;

    if (task_create(&disk_task, disk_driver, NULL) < 0) return -1;
    // O driver deve ter prioridade alta para tratar requisições rapidamente.
    task_setprio(&disk_task, -20);

    // Configura o tratador de sinal para a interrupção do disco.
    struct sigaction action;
    action.sa_handler = disk_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGUSR1, &action, 0) < 0) return -1;

    initialized = 1;
    return 0;
}

// Implementação dos escalonadores de disco
diskrequest_t* disk_scheduler_fcfs() {
    return request_queue;
}

diskrequest_t* disk_scheduler_sstf() {
    if (!request_queue) return NULL;
    
    diskrequest_t *iter = request_queue;
    diskrequest_t *closest = iter;
    int min_distance = abs(iter->block - current_block);
    
    iter = iter->next;
    while (iter != request_queue) {
        int dist = abs(iter->block - current_block);
        if (dist < min_distance) {
            closest = iter;
            min_distance = dist;
        }
        iter = iter->next;
    }
    return closest;
}

diskrequest_t* disk_scheduler_cscan() {
    if (!request_queue) return NULL;

    diskrequest_t *iter = request_queue;
    diskrequest_t *chosen = NULL;
    diskrequest_t *first_in_line = NULL; // O primeiro pedido caso precise dar a volta
    int min_dist_fwd = INT_MAX;
    int min_block_overall = INT_MAX;

    // Procura o mais próximo na direção atual (para frente)
    do {
        if (iter->block >= current_block) {
            int dist = iter->block - current_block;
            if (dist < min_dist_fwd) {
                min_dist_fwd = dist;
                chosen = iter;
            }
        }
        // Guarda o pedido com o menor número de bloco para o caso de "dar a volta"
        if (iter->block < min_block_overall) {
            min_block_overall = iter->block;
            first_in_line = iter;
        }
        iter = iter->next;
    } while (iter != request_queue);

    // Se achou alguém na frente, usa ele. Senão, dá a volta e pega o primeiro.
    if (chosen)
        return chosen;
    else
        return first_in_line;
}

