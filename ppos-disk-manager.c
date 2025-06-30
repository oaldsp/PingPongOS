#include <signal.h> //Para usat SIGUSR1
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "ppos-disk-manager.h"
#include "disk-driver.h"
#include "queue.h"
#include "ppos-core-globals.h"

// Variáveis globais estáticas (sem mudanças)
volatile sig_atomic_t g_disk_interrupt_flag = 0;//Concluiu operação 
static semaphore_t sem_requests; //Requisições pendentes
static task_t disk_task; //tarefa do gerenciador de Disco
static diskrequest_t *request_queue = NULL; //Ponteiro para navegar pelas requisiçoes de Disco
static int initialized = 0; // Flag Paraver se Disco foi inicializado
static int block_size_global = 0;
static int num_blocks_global = 0;
static int current_block = 0;  //Posição atual da cabeça do disco
static int blocks_moved = 0; //Soma a quantidade de blocos que a cabeça se moveu

// Função é chamada quando terminar.
void disk_mgr_shutdown_handler() {
    if (!initialized)//Vejo se está ligado para desligar
        return;

    //Cria uma requisição para finalizar o gerenciador(Requisição Venenosa).
    diskrequest_t *finalization_request = malloc(sizeof(diskrequest_t));
    if (!finalization_request)// Vejo se foi iniciada com sucesso
        return;

    finalization_request->block = -1; // Sinal especial de encerramento
    finalization_request->task = NULL;
    finalization_request->buffer = NULL;
    finalization_request->operation = -1;
    finalization_request->status = 0;
    finalization_request->next = NULL;

    // Adiciona a requisição na fila e acorda o driver para processá-la.
    queue_append((queue_t**)&request_queue, (queue_t*)finalization_request);
    sem_up(&sem_requests);//Acorda a Tarefa para finalizar

    // Espera a tarefa do driver terminar para garantir um desligamento limpo.
    task_join(&disk_task);//Aguardo a tarefa de Disco encerrar

    // Imprime as estatísticas finais.
    printf("Estatísticas Finais -> Total de blocos percorridos: %d\n", blocks_moved);
    
    initialized = 0;
}

void disk_driver(void *arg) {
    (void)arg;//Para Compilador reclamar que não estou usando

    while (1) {
        sem_down(&sem_requests);//Libera o semaforo para não ficar em loop infinito.

        diskrequest_t *req = disk_scheduler_cscan(); //Escolhe proxima requisição de acordo
        //com o padrão de escalonamentp
        // DISCO 1 -> _fcfs(765) _sstf(765) _cscan(765)
        // DISCO 2 -> _fcfs(7681) _sstf(5313) _cscan(11473)
        if (req) {
            queue_remove((queue_t**)&request_queue, (queue_t*)req);
        } else {
            continue;
        }

        // VERIFICA A REQUISIÇÃO DE ENCERRAMENTO
        if (req->block == -1) {
            free(req); // Libera a memória da resuisição de finalização
            break;     // Sai do loop
        }

        //Definea operação e pede para executala 
        int cmd = (req->operation == 0) ? DISK_CMD_READ : DISK_CMD_WRITE;
        disk_cmd(cmd, req->block, req->buffer);

        while (g_disk_interrupt_flag == 0) {
            task_yield();//Libera processar até chegar interrupção do Disco que terminou o serviç0
        }
        g_disk_interrupt_flag = 0;//Reseta flag
        
        req->status = 1;
        blocks_moved += abs(req->block - current_block);
        current_block = req->block;
        
        task_resume(req->task);//Log da tarefa
    }
    // Termina a tarefa do driver.
    task_exit(0);
}

int disk_mgr_init(int *num_blocks, int *block_size) {
    if (initialized) 
        return -1;

    if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0)//Inicia o Disco 
        return -1;

    *num_blocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    *block_size = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);

    if (*num_blocks < 0 || *block_size < 0) 
        return -1;

    num_blocks_global = *num_blocks;
    block_size_global = *block_size;

    if (sem_create(&sem_requests, 0) != 0) 
        return -1;
    if (task_create(&disk_task, disk_driver, NULL) < 0) 
        return -1;
    
    task_setprio(&disk_task, -20);//Coloca prioridade maxima

    //Defineo tradator de interrupeções
    struct sigaction action;
    action.sa_handler = disk_signal_handler;
    action.sa_flags = 0; //Define como flag padrão
    if (sigaction(SIGUSR1, &action, 0) < 0) 
        return -1;

    //Executo o Finalizador de gerenciador de disco
    if (atexit(disk_mgr_shutdown_handler) != 0) {
        perror("Falha ao registrar a função de encerramento do disco");
        return -1;
    }
    
    initialized = 1;
    return 0;
}

int disk_block_read(int block, void *buffer) { 
    
    if (!initialized || block < 0 || block >= num_blocks_global) 
        return -1;
    
    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    
    if (!req) 
        return -1;
    
    req->task = taskExec; //Qual tarefa pediu a leitura 
    req->block = block; //Qual bloco é para ler 
    req->buffer = buffer; //Onde o conteudo lido deve serarmazenado
    req->operation = 0; //Defino como leitura
    req->status = 0; //Coloco como pendente.
    req->next = NULL;

    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests);//Ativo o semaforo
    
    task_suspend(taskExec, NULL); //Suspende tarefa mãe ate terminar leitura
    task_yield();//libera o processador para a próxima tarefa

    int result = (req->status == 1) ? 0 : -1;//Pega status da requisição 
    
    free(req);
    return result;
}

int disk_block_write(int block, void *buffer) { 

    if (!initialized || block < 0 || block >= num_blocks_global) 
        return -1;
    
    diskrequest_t *req = malloc(sizeof(diskrequest_t));
    
    if (!req) 
        return -1;
   
    req->task = taskExec; //Qual tarefa pediu a escrita
    req->block = block; // Em qual bloco do discosera escrito
    req->buffer = buffer; //Qual o dado que sera escrito
    req->operation = 1; //Operação de escrita
    req->status = 0; //Status pendente
    req->next = NULL;
    
    queue_append((queue_t**)&request_queue, (queue_t*)req);
    sem_up(&sem_requests);//Ativo o semaforo
    
    task_suspend(taskExec, NULL);//Suspende tarefa mãe ate terminar leitura
    task_yield();//libera o processador para a próxima tarefa
    
    int result = (req->status == 1) ? 0 : -1;//Pega status da requisição
    
    free(req); 
    return result;
}

//Função do Sinal de interrupeção
void disk_signal_handler(int signum) { 
    if (signum == SIGUSR1) 
        g_disk_interrupt_flag = 1; 
}

/*================ ESCALONADORES DE DISCO =================*/
//First Come, First Served (FCFS):
diskrequest_t* disk_scheduler_fcfs() { 
    return request_queue;//pega primeiro da fila. 
}

//Shortest Seek-Time First (SSTF):
diskrequest_t* disk_scheduler_sstf() { 
    if (!request_queue) 
        return NULL;
    
    diskrequest_t   *iter = request_queue, *closest = iter;
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

//Circular Scan (CSCAN):
diskrequest_t* disk_scheduler_cscan() {
    if (!request_queue) 
        return NULL;

    diskrequest_t *iter = request_queue, *chosen = NULL, *first_in_line = NULL;
    
    int min_dist = INT_MAX, return_block = INT_MAX;
    do {
        if (iter->block >= current_block) {
            //pega bloco mais perto que esteja a frente da fila
            int dist = iter->block - current_block;
            if (dist < min_dist) { 
                min_dist = dist; 
                chosen = iter; 
            }
        }
        if (iter->block < return_block) {
            //Se achar um bloco menor que ode retorno atual ele passa a ser. 
            return_block = iter->block; 
            first_in_line = iter; 
        }
        iter = iter->next;
    } while (iter != request_queue);

    return chosen ? chosen : first_in_line;//Se não achar ninguem retorna o primeiro
}

