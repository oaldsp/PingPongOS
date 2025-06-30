# Nome do executável
#TARGET1 = ppos-test-preempcao
#TARGET2 = ppos-test-scheduler
#TARGET3 = ppos-test-preempcao-stress
#TARGET4 = ppos-test-contab-prio

# Fontes e objetos
#OBJS1 = ppos-core-aux.c pingpong-preempcao.c disk-driver.o ppos-all.o ppos-disk-manager.o queue.o
#OBJS2 = ppos-core-aux.c pingpong-scheduler.c disk-driver.o ppos-all.o ppos-disk-manager.o queue.o
#OBJS3 = ppos-core-aux.c pingpong-preempcao-stress.c disk-driver.o ppos-all.o ppos-disk-manager.o queue.o
#OBJS4 = ppos-core-aux.c pingpong-contab-prio.c disk-driver.o ppos-all.o ppos-disk-manager.o queue.o

# Compilador e flags
#CC = gcc
#CFLAGS = -std=gnu99

# Regra padrão (target 'all')
#all:
#	$(CC) $(CFLAGS) -o $(TARGET1) $(OBJS1) -lrt
#	$(CC) $(CFLAGS) -o $(TARGET2) $(OBJS2) -lrt
#	$(CC) $(CFLAGS) -o $(TARGET3) $(OBJS3) -lrt
#	$(CC) $(CFLAGS) -o $(TARGET4) $(OBJS4) -lrt
#	$(CC)

all:
	gcc -Wall ppos-disk-manager.c pingpong-disco2.c ppos-core-aux.c disk-driver.o queue.o ppos-all.o -o p2 -lrt
