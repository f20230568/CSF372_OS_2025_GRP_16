/* server.c - BONUS: Crash Recovery & Undo */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <sys/time.h>
#include <mqueue.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>

#define GRID_SIZE 10
#define MAX_STRING_LEN 64
#define MQ_NAME_SERVER "/osdoc_server_mq"
#define MQ_NAME_CLIENT_PREFIX "/osdoc_client_"
#define MAX_MSG_SIZE 512
#define MQ_MAXMSG 10 
#define MAX_CLIENTS 10

/* Protocol Constants */
#define REQ_READ 1
#define REQ_WRITE 2
#define REQ_SHUTDOWN 3
#define REQ_UNDO 4  /* BONUS: New Command */

#define RESP_SUCCESS 0
#define RESP_DROPPED 1
#define RESP_ERROR 2

/* --- Undo History Stack (Bonus) --- */
typedef struct HistoryNode {
    int line;
    int pos;
    char prev_word[MAX_STRING_LEN + 1];
    struct HistoryNode *next;
} HistoryNode;

HistoryNode *history_head = NULL;

/* --- Data Structures --- */
typedef struct {
    int req_type; int client_id; int line; int pos;
    char word[MAX_STRING_LEN + 1]; int duration_ms; int is_print;
} request_t;

typedef struct {
    int status; char word[MAX_STRING_LEN + 1];
} response_t;

typedef struct {
    int line; int pos; int client_id; int duration_ms;
} lock_release_arg_t;

/* --- Globals --- */
static char doc[GRID_SIZE][GRID_SIZE][MAX_STRING_LEN + 1];
static int lock_owner[GRID_SIZE][GRID_SIZE]; 
static pthread_mutex_t doc_mutex = PTHREAD_MUTEX_INITIALIZER;
static mqd_t server_mqd = (mqd_t)-1;
static volatile sig_atomic_t keep_running = 1;
static int active_clients[MAX_CLIENTS] = {0};
static int seen_any_client = 0;

/* --- Helper Functions --- */
static void ts_printf(const char *fmt, ...) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm_info);
    fprintf(stdout, "%s.%03ld ", tbuf, tv.tv_usec / 1000);
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fflush(stdout);
}
#define printf(...) ts_printf(__VA_ARGS__)

void handle_sig(int sig) { (void)sig; keep_running = 0; }

static void send_response(int client_id, response_t *resp) {
    char mqname[64];
    snprintf(mqname, sizeof(mqname), "%s%d", MQ_NAME_CLIENT_PREFIX, client_id);
    mqd_t mq = mq_open(mqname, O_WRONLY);
    if (mq == (mqd_t)-1) return; 
    mq_send(mq, (const char*)resp, sizeof(response_t), 0);
    mq_close(mq);
}

static void *lock_releaser(void *arg) {
    lock_release_arg_t a = *(lock_release_arg_t*)arg;
    free(arg);
    struct timespec ts;
    ts.tv_sec = a.duration_ms / 1000;
    ts.tv_nsec = (a.duration_ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
    pthread_mutex_lock(&doc_mutex);
    if (lock_owner[a.line][a.pos] == a.client_id + 1) {
        lock_owner[a.line][a.pos] = 0;
        printf("Server: Client %d UNLOCK(%d,%d)\n", a.client_id, a.line, a.pos);
    }
    pthread_mutex_unlock(&doc_mutex);
    return NULL;
}

/* --- BONUS: PERSISTENCE FUNCTIONS --- */
void log_change(int line, int pos, const char *word) {
    FILE *f = fopen("recovery.log", "a");
    if (f) {
        fprintf(f, "%d %d %s\n", line, pos, word);
        fclose(f);
    }
}

void recover_state() {
    FILE *f = fopen("recovery.log", "r");
    if (!f) {
        printf("Server: No recovery log found. Starting fresh.\n");
        return;
    }
    printf("Server: Recovering state from log...\n");
    int line, pos;
    char word[MAX_STRING_LEN + 1];
    while (fscanf(f, "%d %d %64s", &line, &pos, word) == 3) {
        if (line >= 0 && line < GRID_SIZE && pos >= 0 && pos < GRID_SIZE) {
            strncpy(doc[line][pos], word, MAX_STRING_LEN);
        }
    }
    fclose(f);
    printf("Server: Recovery complete.\n");
}

/* --- BONUS: UNDO FUNCTIONS --- */
void push_history(int line, int pos, const char *old_word) {
    HistoryNode *node = malloc(sizeof(HistoryNode));
    if (!node) return;
    node->line = line;
    node->pos = pos;
    strncpy(node->prev_word, old_word, MAX_STRING_LEN);
    node->next = history_head;
    history_head = node;
}

void write_output_file() {
    FILE *f = fopen("output.txt", "w");
    if (f) {
        for (int i = 0; i < GRID_SIZE; ++i) {
            int any = 0;
            for (int j = 0; j < GRID_SIZE; ++j) if (doc[i][j][0]) any = 1;
            if (!any) continue;
            int first = 1;
            for (int j = 0; j < GRID_SIZE; ++j) {
                if (doc[i][j][0]) {
                    if (!first) fprintf(f, " ");
                    fprintf(f, "%s", doc[i][j]);
                    first = 0;
                }
            }
            fprintf(f, "\n");
        }
        fclose(f);
    }
}

int main(void) {
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    memset(doc, 0, sizeof(doc));
    memset(lock_owner, 0, sizeof(lock_owner));

    /* BONUS: Recover from crash */
    recover_state();

    struct mq_attr attr = {0, MQ_MAXMSG, MAX_MSG_SIZE, 0};
    mq_unlink(MQ_NAME_SERVER);
    server_mqd = mq_open(MQ_NAME_SERVER, O_CREAT | O_RDONLY, 0666, &attr);
    if (server_mqd == (mqd_t)-1) { perror("Server mq_open"); return 1; }

    printf("Server: Started. Grid initialized.\n");
    char buf[MAX_MSG_SIZE];
    while (keep_running) {
        ssize_t r = mq_receive(server_mqd, buf, MAX_MSG_SIZE, NULL);
        if (r == -1) { if (errno == EINTR) break; break; }
        if (r < (ssize_t)sizeof(request_t)) continue;

        request_t req;
        memcpy(&req, buf, sizeof(request_t));

        /* Lifecycle tracking */
        if (req.client_id >= 0 && req.client_id < MAX_CLIENTS) {
            seen_any_client = 1;
            if (req.req_type != REQ_SHUTDOWN) active_clients[req.client_id] = 1;
        }

        if (req.req_type == REQ_SHUTDOWN) {
            if (req.client_id >= 0 && req.client_id < MAX_CLIENTS) active_clients[req.client_id] = 0;
            if (seen_any_client) {
                int any_active = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) if (active_clients[i]) any_active = 1;
                if (!any_active) break;
            }
            continue;
        }

        /* Handle UNDO (Bonus) */
        if (req.req_type == REQ_UNDO) {
            response_t resp;
            pthread_mutex_lock(&doc_mutex);
            
            if (history_head == NULL) {
                resp.status = RESP_ERROR;
                printf("Server: Client %d UNDO FAILED (History Empty)\n", req.client_id);
            } else {
                /* Pop last action */
                HistoryNode *node = history_head;
                history_head = node->next;
                
                /* Check if locked */
                if (lock_owner[node->line][node->pos] != 0) {
                    resp.status = RESP_DROPPED; // Cannot undo locked word
                    printf("Server: Client %d UNDO DROPPED (Word Locked)\n", req.client_id);
                } else {
                    strncpy(doc[node->line][node->pos], node->prev_word, MAX_STRING_LEN);
                    log_change(node->line, node->pos, node->prev_word);
                    resp.status = RESP_SUCCESS;
                    printf(
                        "Server: Client %d UNDO(%d,%d) -> '%s'\n", 
                        req.client_id, node->line, node->pos, node->prev_word
                    );
                }
                free(node);
            }
            pthread_mutex_unlock(&doc_mutex);
            send_response(req.client_id, &resp);
            continue;
        }

        if (req.line < 0 || req.line >= GRID_SIZE || req.pos < 0 || req.pos >= GRID_SIZE) continue;

        response_t resp;
        pthread_mutex_lock(&doc_mutex);
        int owner = lock_owner[req.line][req.pos];

        if (req.req_type == REQ_READ) {
            if (owner != 0 && owner != req.client_id + 1) {
                resp.status = RESP_DROPPED;
                resp.word[0] = '\0';
                if (!req.is_print) printf("Server: Client %d READ LOCK(%d,%d) DENIED\n", req.client_id, req.line, req.pos);
            } else {
                resp.status = RESP_SUCCESS;
                strncpy(resp.word, doc[req.line][req.pos], MAX_STRING_LEN);
                if (!req.is_print) {
                     printf("Server: Client %d READ LOCK(%d,%d) GRANTED\n", req.client_id, req.line, req.pos);
                     printf("Server: Client %d READ(%d,%d) SUCCESS - Value: '%s'\n", req.client_id, req.line, req.pos, resp.word);
                }
            }
            pthread_mutex_unlock(&doc_mutex);
            send_response(req.client_id, &resp);

        } else if (req.req_type == REQ_WRITE) {
            if (owner != 0) {
                resp.status = RESP_DROPPED;
                pthread_mutex_unlock(&doc_mutex);
                printf("Server: Client %d WRITE LOCK(%d,%d) DENIED\n", req.client_id, req.line, req.pos);
                send_response(req.client_id, &resp);
            } else {
                /* BONUS: Save old state to history before overwriting */
                push_history(req.line, req.pos, doc[req.line][req.pos]);

                lock_owner[req.line][req.pos] = req.client_id + 1;
                strncpy(doc[req.line][req.pos], req.word, MAX_STRING_LEN);
                
                log_change(req.line, req.pos, req.word);

                resp.status = RESP_SUCCESS;
                pthread_mutex_unlock(&doc_mutex);

                printf(
                    "Server: Client %d WRITE LOCK(%d,%d) GRANTED - Value: '%s', sleeping for %dms\n",
                    req.client_id, req.line, req.pos, req.word, req.duration_ms
                );
                
                send_response(req.client_id, &resp);

                lock_release_arg_t *arg = malloc(sizeof(lock_release_arg_t));
                if (arg) {
                    arg->line = req.line; arg->pos = req.pos;
                    arg->client_id = req.client_id; arg->duration_ms = req.duration_ms;
                    pthread_t tid;
                    pthread_create(&tid, NULL, lock_releaser, arg);
                    pthread_detach(tid);
                }
            }
        }
    }
    write_output_file();
    mq_close(server_mqd);
    mq_unlink(MQ_NAME_SERVER);
    printf("Server: Shutdown complete\n");
    return 0;
}
