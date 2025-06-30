void tratador (int signum)
{
  if(taskExec->task_sys==0){
    contador--;
    if(contador==0){
        //taskExec->time_proce+=systime()- taskExec->time_aux;
        task_yield();//libera o processador para a próxima tarefa
    }
  }
}

void after_task_create (task_t *task) {
    // put your customization here
#ifdef DEBUG
    printf("\ntask_create - AFTER - [%d]", task->id);
#endif

task->task_sys=0;//Tarefa de usuaro

if (!task->task_sys) {
    task->state = PPOS_TASK_STATE_READY;
    queue_append((queue_t**)&readyQueue, (queue_t*)task);
}

task->prio_base=-21;//Prioridade base
task->time_proce=0;//Soma de tempo ativo
task->cont_ativo=0;//Quantas vezes ficou ativo 
task->time_ini=systime();//Tempo que começou
}

task_t* scheduler() {
    if (readyQueue == NULL) {
        return NULL;
    }

    task_t* task_prio = readyQueue;
    task_t* aux = readyQueue->next; // Começa do segundo elemento

    // 1. Encontra a tarefa de maior prioridade (menor valor)
    // Este laço percorre a fila inteira
    do {
        if (task_getprio(aux) < task_getprio(task_prio)) {
            task_prio = aux;
        }
        aux = aux->next;
    } while (aux != readyQueue);

    // 2. Envelhece todas as outras tarefas
    aux = readyQueue;
    do {
        // Envelhece a tarefa se ela NÃO for a escolhida
        if (aux != task_prio) {
            int prio = task_getprio(aux);
            if (prio > -20) { // Garante que a prioridade não ultrapasse o limite
                task_setprio(aux, prio - 1);
            }
        }
        aux = aux->next;
    } while (aux != readyQueue);

    // 3. Reseta a prioridade da tarefa escolhida e o quantum
    task_setprio(task_prio, task_prio->prio_base);
    contador = 20; // Reseta o quantum do timer
    task_prio->cont_ativo++;
    task_prio->time_aux = systime();
    
    return task_prio;
}

unsigned int systime() {
    struct timeval now;
    gettimeofday(&now, NULL);

    // Usa long long para evitar overflow com números grandes
    long long start_ms = (long long)start_time.tv_sec * 1000 + start_time.tv_usec / 1000;
    long long now_ms = (long long)now.tv_sec * 1000 + now.tv_usec / 1000;

    return (unsigned int)(now_ms - start_ms);
}
