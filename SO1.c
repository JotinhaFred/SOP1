/* ============================================================
 * pix.c — INF0336 A01 — SOLUÇÃO UNIFICADA (Questões 1, 2 e 3)
 * Compile: gcc -Wall -o pix pix.c -lpthread
 *
 * APRESENTAÇÃO ITERATIVA (uma feature por vez):
 *   ./pix corrida <N>  -> Q1: thread pool N:1 SEM proteção (condição de corrida)
 *   ./pix fcfs    <N>  -> Q2: escalonador FCFS + proteção mínima (mutex)
 *   ./pix debug   <N>  -> Q3: FCFS + mutex + debugging por sinais (Ctrl+\ ou kill -USR1)
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

/* ---------- SLEEPS AUMENTADOS (microssegundos) ---------- */
#define JANELA_CORRIDA      50000    /* 50 ms  janela leitura->escrita SEM proteção */
#define TEMPO_TRANSFERENCIA 1000000  /* 1 s    processamento dentro da seção crítica protegida     */
#define TEMPO_TROCA         10000    /* 10 ms  overhead entre requisições                          */

/* ---------- Dimensões do sistema ---------- */
#define NUM_CONTAS    5
#define SALDO_INICIAL 1000
#define MAX_REQ       200
#define MAX_THREADS   64

/* ---------- Requisição PIX ---------- */
typedef struct { int origem, destino, valor, id, estado; } Req;
/* estado: 0=PENDENTE, 1=EXECUTANDO, 2=CONCLUIDA */

/* ---------- RECURSO compartilhado: base de contas ---------- */
int contas[NUM_CONTAS];
Req fila[MAX_REQ];
int frente = 0, tras = 0;

/* ---------- Sincronização ---------- */
pthread_mutex_t mtx_contas = PTHREAD_MUTEX_INITIALIZER;  /* protege o recurso   */
pthread_mutex_t mtx_fila   = PTHREAD_MUTEX_INITIALIZER;  /* protege a fila FCFS */

/* ---------- Configuração da execução ---------- */
int modo;        /* 1=corrida, 2=fcfs, 3=debug */
int n_threads;

/* ---------- Rastreamento do pool (para o debugging) ---------- */
typedef struct { int ocupada; int req_atual; } InfoThread;
InfoThread pool[MAX_THREADS];

/* ============================================================
 * Q3 — HANDLER DE SINAL: mostra o estado atual da fila
 * ============================================================ */
void handler_debug(int sig) {
    char buf[8192]; int p = 0;
    p += sprintf(buf+p, "\n########## DEBUG acionado via sinal (%d) ##########\n", sig);

    p += sprintf(buf+p, "--- Fila FCFS (%d pendentes, %d atendidas) ---\n", tras-frente, frente);
    for (int i = 0; i < tras; i++) {
        const char *e = fila[i].estado==0 ? "PENDENTE" :
                        fila[i].estado==1 ? "EXECUTANDO" : "CONCLUIDA";
        p += sprintf(buf+p, " Req#%-3d | %d -> %d | valor=%-3d | %s\n",
                     fila[i].id, fila[i].origem, fila[i].destino, fila[i].valor, e);
    }

    p += sprintf(buf+p, "--- Thread Pool ---\n");
    for (int i = 0; i < n_threads; i++)
        p += sprintf(buf+p, " Worker %d: %s (Req#%d)\n", i,
                     pool[i].ocupada ? "OCUPADA" : "OCIOSA", pool[i].req_atual);

    /* trylock evita deadlock se o sinal chegar durante a seção crítica */
    if (pthread_mutex_trylock(&mtx_contas) == 0) {
        int t = 0;
        p += sprintf(buf+p, "--- Saldos ---\n");
        for (int i = 0; i < NUM_CONTAS; i++) { p += sprintf(buf+p, " conta %d: %d\n", i, contas[i]); t += contas[i]; }
        p += sprintf(buf+p, " TOTAL: %d\n", t);
        pthread_mutex_unlock(&mtx_contas);
    } else {
        p += sprintf(buf+p, "--- Saldos: mutex ocupado (seção crítica em execução) ---\n");
    }
    p += sprintf(buf+p, "####################################################\n\n");
    write(STDOUT_FILENO, buf, p);   /* write() é async-signal-safe */
}

/* ============================================================
 * WORKER do thread pool (atende a fila em ordem FCFS)
 * ============================================================ */
void *worker(void *arg) {
    int id = *(int *)arg;

    while (1) {
        /* ---_retira_ a próxima requisição da fila (ordem de chegada) --- */
        pthread_mutex_lock(&mtx_fila);
        if (frente >= tras) { pthread_mutex_unlock(&mtx_fila); break; }
        Req r = fila[frente];
        fila[frente].estado = 1;
        frente++;
        pthread_mutex_unlock(&mtx_fila);

        pool[id].ocupada = 1; pool[id].req_atual = r.id;
        printf("[Worker %d] atendeu Req#%d: %d -> %d, valor=%d\n",
               id, r.id, r.origem, r.destino, r.valor);

        if (modo == 1) {
            /* ===== Q1: SEÇÃO CRÍTICA SEM PROTEÇÃO =====
               RECURSO: contas[]  |  CORRIDA: leitura -> sleep -> escrita */
            int so = contas[r.origem];
            int sd = contas[r.destino];
            usleep(JANELA_CORRIDA);              /* sleep GRANDE = janela de corrida larga */
            if (so >= r.valor) {
                contas[r.origem]  = so - r.valor;   /* atualização perdida se houver sobreposição */
                contas[r.destino] = sd + r.valor;
            }
        } else {
            /* ===== Q2/Q3: SEÇÃO CRÍTICA PROTEGIDA POR MUTEX ===== */
            pthread_mutex_lock(&mtx_contas);
            int so = contas[r.origem];
            int sd = contas[r.destino];
            usleep(TEMPO_TRANSFERENCIA);         /* sleep grande p/ observar escalonamento/debug */
            if (so >= r.valor) {
                contas[r.origem]  = so - r.valor;
                contas[r.destino] = sd + r.valor;
            }
            pthread_mutex_unlock(&mtx_contas);
        }

        fila[r.id-1].estado = 2;
        pool[id].ocupada = 0; pool[id].req_atual = -1;
        usleep(TEMPO_TROCA);
    }
    return NULL;
}

/* ---------- Gera requisições e enfileira (ordem de chegada = FCFS) ---------- */
void gerar(int qtd) {
    for (int i = 0; i < qtd; i++) {
        fila[tras].origem  = rand() % NUM_CONTAS;
        fila[tras].destino = rand() % NUM_CONTAS;
        while (fila[tras].destino == fila[tras].origem)
            fila[tras].destino = rand() % NUM_CONTAS;
        fila[tras].valor = (rand() % 50) + 10;
        fila[tras].id    = tras + 1;
        fila[tras].estado = 0;
        tras++;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Uso: %s corrida|fcfs|debug <N_threads>\n", argv[0]);
        return 1;
    }
    if      (strcmp(argv[1], "corrida") == 0) modo = 1;
    else if (strcmp(argv[1], "fcfs")    == 0) modo = 2;
    else if (strcmp(argv[1], "debug")   == 0) modo = 3;
    else { printf("Modo inválido.\n"); return 1; }

    n_threads = atoi(argv[2]);
    if (n_threads < 1 || n_threads > MAX_THREADS) { printf("N inválido.\n"); return 1; }

    srand(42);                                   /* seed fixa: testes reproduzíveis */
    for (int i = 0; i < NUM_CONTAS; i++) contas[i] = SALDO_INICIAL;
    for (int i = 0; i < n_threads; i++) { pool[i].ocupada = 0; pool[i].req_atual = -1; }

    int total_req = (modo == 1) ? 200 : 12;      /* corrida: volume p/ estatística; fcfs/debug: observável */
    gerar(total_req);

    if (modo == 3) {                             /* Q3: registra os sinais de debugging */
        struct sigaction sa;
        sa.sa_handler = handler_debug;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, NULL);           /* via: kill -USR1 <pid> */
        sigaction(SIGQUIT, &sa, NULL);           /* via: tecla Ctrl+\ no terminal */
        printf("[debug] PID=%d — pressione Ctrl+\\ aqui ou rode: kill -USR1 %d\n\n", getpid(), getpid());
    }

    printf("=== Modo %s | N=%d threads | %d requisições ===\n\n", argv[1], n_threads, total_req);

    pthread_t th[MAX_THREADS]; int ids[MAX_THREADS];
    for (int i = 0; i < n_threads; i++) { ids[i] = i; pthread_create(&th[i], NULL, worker, &ids[i]); }
    for (int i = 0; i < n_threads; i++) pthread_join(th[i], NULL);

    int total = 0;
    for (int i = 0; i < NUM_CONTAS; i++) total += contas[i];
    printf("\n===== RESUMO FINAL (modo=%s, N=%d) =====\n", argv[1], n_threads);
    printf("Saldo total inicial: %d\n", NUM_CONTAS * SALDO_INICIAL);
    printf("Saldo total final:   %d\n", total);
    printf("Diferença:           %d\n", total - NUM_CONTAS * SALDO_INICIAL);
    if (modo == 1)
        printf(total != NUM_CONTAS*SALDO_INICIAL ? ">> CONDIÇÃO DE CORRIDA EVIDENCIADA!\n"
                                                 : ">> Sem inconsistência nesta execução.\n");
    else
        printf(">> Mutex preservou a integridade do sistema.\n");
    return 0;
}