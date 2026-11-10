/* ============================================================
 * pix.c — INF0336 A01 — SOLUÇÃO UNIFICADA (Questões 1, 2 e 3)
 * Compile: gcc -Wall -o pix pix.c -lpthread
 *
 *   ./pix corrida <N>  -> Q1: thread pool SEM proteção (corrida)
 *   ./pix fcfs    <N>  -> Q2: FCFS + mutex
 *   ./pix debug   <N>  -> Q3: debugging por sinais:
 *                           [NOVO] QUALQUER TECLA no próprio terminal
 *                                  Ctrl+\            (SIGQUIT)
 *                                  kill -USR1 <pid>  (outro terminal)
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>   /* NOVO: terminal não-canônico (POSIX/libc, não é lib extra) */

/* ---------- Sleeps aumentados (µs) ---------- */
#define JANELA_CORRIDA      50000
#define TEMPO_TRANSFERENCIA 1000000
#define TEMPO_TROCA         10000

/* ---------- Dimensões ---------- */
#define NUM_CONTAS    5
#define SALDO_INICIAL 1000
#define MAX_REQ       200
#define MAX_THREADS   64

typedef struct { int origem, destino, valor, id, estado; } Req;

int contas[NUM_CONTAS];
Req fila[MAX_REQ];
int frente = 0, tras = 0;

pthread_mutex_t mtx_contas = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_fila   = PTHREAD_MUTEX_INITIALIZER;

int modo;
int n_threads;

typedef struct { int ocupada; int req_atual; } InfoThread;
InfoThread pool[MAX_THREADS];

/* ---------- NOVO: estado do terminal (Q3) ---------- */
static struct termios termo_original;
static int termo_alterado = 0;

/* ============================================================
 * HANDLER DE SINAL (inalterado): imprime estado da fila
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
    write(STDOUT_FILENO, buf, p);
}

/* ============================================================
 * NOVO (Q3): TERMINAL EM MODO TECLA ÚNICA
 * Sem ICANON: não espera Enter | Sem ECHO: não mostra a tecla
 * ============================================================ */
void terminal_restaurar(void) {
    if (termo_alterado)
        tcsetattr(STDIN_FILENO, TCSANOW, &termo_original);
}

void terminal_tecla_unica(void) {
    if (!isatty(STDIN_FILENO)) return;                 /* stdin não é terminal: mantém só Ctrl+\ e kill */
    if (tcgetattr(STDIN_FILENO, &termo_original) != 0) return;

    struct termios t = termo_original;
    t.c_lflag &= ~(ICANON | ECHO);                     /* MUDANÇA 1: modo não-canônico */
    t.c_cc[VMIN]  = 1;                                 /* read() retorna com 1 caractere */
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);

    termo_alterado = 1;
    atexit(terminal_restaurar);                        /* MUDANÇA 2: restaura o shell ao sair */
}

/* ============================================================
 * NOVO (Q3): THREAD OUVINTE — converte a TECLA em SINAL
 * (mantém o requisito "debugging por sinais" da questão)
 * ============================================================ */
void *thread_teclado(void *arg) {
    (void)arg;
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1)             /* MUDANÇA 3: bloqueia até 1 tecla */
        raise(SIGUSR1);                                /* tecla -> sinal -> handler_debug */
    /* Para restringir a uma tecla específica, troque por:
       if (c == 'd') raise(SIGUSR1);                    */
    return NULL;
}

/* ============================================================
 * WORKER do thread pool (inalterado)
 * ============================================================ */
void *worker(void *arg) {
    int id = *(int *)arg;

    while (1) {
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
            int so = contas[r.origem];
            int sd = contas[r.destino];
            usleep(JANELA_CORRIDA);
            if (so >= r.valor) {
                contas[r.origem]  = so - r.valor;
                contas[r.destino] = sd + r.valor;
            }
        } else {
            pthread_mutex_lock(&mtx_contas);
            int so = contas[r.origem];
            int sd = contas[r.destino];
            usleep(TEMPO_TRANSFERENCIA);
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
    if (argc < 3) { printf("Uso: %s corrida|fcfs|debug <N_threads>\n", argv[0]); return 1; }
    if      (strcmp(argv[1], "corrida") == 0) modo = 1;
    else if (strcmp(argv[1], "fcfs")    == 0) modo = 2;
    else if (strcmp(argv[1], "debug")   == 0) modo = 3;
    else { printf("Modo inválido.\n"); return 1; }

    n_threads = atoi(argv[2]);
    if (n_threads < 1 || n_threads > MAX_THREADS) { printf("N inválido.\n"); return 1; }

    srand(42);
    for (int i = 0; i < NUM_CONTAS; i++) contas[i] = SALDO_INICIAL;
    for (int i = 0; i < n_threads; i++) { pool[i].ocupada = 0; pool[i].req_atual = -1; }

    int total_req = (modo == 1) ? 200 : 12;
    gerar(total_req);

    if (modo == 3) {
        struct sigaction sa;
        sa.sa_handler = handler_debug;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);

        terminal_tecla_unica();                                   /* NOVO */
        pthread_t tcl;
        pthread_create(&tcl, NULL, thread_teclado, NULL);         /* NOVO */
        pthread_detach(tcl);                                      /* NOVO: vive até o fim do processo */

        printf("[debug] PID=%d\n", getpid());
        printf("[debug] Dispare o dump: QUALQUER TECLA aqui | Ctrl+\\ | kill -USR1 %d\n\n", getpid());
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
    return 0;   /* atexit() restaura o terminal automaticamente */
}