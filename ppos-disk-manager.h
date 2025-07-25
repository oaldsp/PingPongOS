// PingPongOS - PingPong Operating System
// Prof. Carlos A. Maziero, DINF UFPR
// Versão 1.2 -- Julho de 2017

// interface do gerente de disco rígido (block device driver)

#ifndef __DISK_MGR__
#define __DISK_MGR__

#include "ppos.h" // Garante que task_t e semaphore_t são conhecidos
//#include <signal.h>

//extern volatile sig_atomic_t g_disk_interrupt_flag;
//extern semaphore_t sem_disk;

//#define DEBUG_DISK 1

// estruturas de dados e rotinas de inicializacao e acesso
// a um dispositivo de entrada/saida orientado a blocos,
// tipicamente um disco rigido.

// structura de dados que representa um pedido de leitura/escrita ao disco
typedef struct diskrequest_t {
    struct diskrequest_t* next; // Ponteiro para o próximo na fila
    struct diskrequest_t* prev; // Ponteiro para o anterior na fila

    task_t* task; // Tarefa que solicitou a operação
    unsigned char operation; // DISK_REQUEST_READ ou DISK_REQUEST_WRITE(Leitura ou escrita)
    int block;  // Bloco a ser lido/escrito
    void* buffer;  // Endereço dos dados a escrever no disco, ou onde devem ser 
    //colocados os dados lidos do disco;
    volatile int status; // Status: 0=pendente, 1=concluído, -1=erro
} diskrequest_t;

// estrutura que representa um disco no sistema operacional
// structura de dados que representa o disco para o SO
typedef struct {
    int numBlocks;
    int blockSize;

    semaphore_t semaforo;

    unsigned char sinal;
    unsigned char livre;

    task_t* diskQueue;
    semaphore_t semaforo_queue;
    diskrequest_t* requestQueue;
} disk_t;

// inicializacao do gerente de disco
// retorna -1 em erro ou 0 em sucesso
// numBlocks: tamanho do disco, em blocos
// blockSize: tamanho de cada bloco do disco, em bytes
int disk_mgr_init (int *numBlocks, int *blockSize) ;

// leitura de um bloco, do disco para o buffer
int disk_block_read (int block, void *buffer) ;

// escrita de um bloco, do buffer para o disco
int disk_block_write (int block, void *buffer) ;

// Finaliza o gerente de disco, liberando recursos e terminando a tarefa do driver.
//int disk_mgr_close();
void disk_mgr_shutdown_handler();

// escalonador de requisições do disco
//diskrequest_t* disk_scheduler();

/*MINHAS*/
void disk_driver(void *arg);
void disk_signal_handler(int signum);
diskrequest_t* disk_scheduler_fcfs();
diskrequest_t* disk_scheduler_sstf();
diskrequest_t* disk_scheduler_cscan();

#endif
