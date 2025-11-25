/* client.c - BONUS: Supports UNDO command */

#define _POSIX_C_SOURCE 199309L 
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

#define REQ_READ 1
#define REQ_WRITE 2
#define REQ_SHUTDOWN 3 
#define REQ_UNDO 4 /* BONUS */

#define RESP_SUCCESS 0
#define RESP_DROPPED 1
#define RESP_ERROR 2

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

typedef struct {
    int req_type; int client_id; int line; int pos;
    char word[MAX_STRING_LEN + 1]; int duration_ms; int is_print;
} request_t;

typedef struct {
    int status; char word[MAX_STRING_LEN + 1];
} response_t;

mqd_t server_mqd = (mqd_t)-1;
mqd_t client_mqd = (mqd_t)-1;
pthread_mutex_t comm_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t running = 1;

static void get_timeout_ts(struct timespec *ts) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += 2;
}

static int send_request_and_wait_response(request_t *req, response_t *resp) {
    pthread_mutex_lock(&comm_mutex);
    if (mq_send(server_mqd, (const char*)req, sizeof(request_t), 0) == -1) {
        pthread_mutex_unlock(&comm_mutex); resp->status = RESP_ERROR; return -1;
    }
    struct timespec ts;
    get_timeout_ts(&ts);
    if (mq_timedreceive(client_mqd, (char*)resp, MAX_MSG_SIZE, NULL, &ts) == -1) {
        pthread_mutex_unlock(&comm_mutex); resp->status = RESP_ERROR; return -1;
    }
    pthread_mutex_unlock(&comm_mutex);
    return 0;
}

static void *print_doc_thread(void *arg) {
    int cid = *(int*)arg;
    free(arg);
    request_t req; response_t resp;

    char outname[64];
    snprintf(outname, sizeof(outname), "output_client%d.txt", cid);

    while (running) {
        sleep(2);
        if (!running) break;

        FILE *of = fopen(outname, "w");
        if (!of) continue;
        for (int line = 0; line < GRID_SIZE; ++line) {
            int any = 0;
            char linebuf[1024] = "";
            for (int pos = 0; pos < GRID_SIZE; ++pos) {
                req.req_type = REQ_READ; req.client_id = cid; req.line = line;
                req.pos = pos; req.is_print = 1; req.word[0] = '\0';
                int res = send_request_and_wait_response(&req, &resp);
                if (res == -1 || resp.status != RESP_SUCCESS) {
                    if (any) strcat(linebuf, " "); strcat(linebuf, "???"); any = 1;
                } else {
                    if (resp.word[0] != '\0') {
                        if (any) strcat(linebuf, " "); strcat(linebuf, resp.word); any = 1;
                    }
                }
            }
            if (any) fprintf(of, "%s\n", linebuf);
        }
        fclose(of);
        printf("Client %d: PRINT_DOC written to %s\n", cid, outname);
    }
    return NULL;
}

static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int cid = atoi(argv[1]);
    const char *input_filename = (argc >= 3) ? argv[2] : "input.txt";

    int retries = 10;
    while (1) {
        server_mqd = mq_open(MQ_NAME_SERVER, O_WRONLY);
        if (server_mqd != (mqd_t)-1) break;
        if (retries-- <= 0) return 1;
        usleep(500000);
    }

    char mqname[64];
    snprintf(mqname, sizeof(mqname), "%s%d", MQ_NAME_CLIENT_PREFIX, cid);
    mq_unlink(mqname);

    struct mq_attr attr = {0, MQ_MAXMSG, MAX_MSG_SIZE, 0};
    client_mqd = mq_open(mqname, O_CREAT | O_RDONLY, 0666, &attr);
    if (client_mqd == (mqd_t)-1) return 1;

    usleep((10 - cid) * 200000);
    printf("Client %d: Starting\n", cid);

    pthread_t ptid;
    int *parg = malloc(sizeof(int));
    *parg = cid;
    pthread_create(&ptid, NULL, print_doc_thread, parg);

    FILE *f = fopen(input_filename, "r");
    if (f) {
        char linebuf[512];
        while (fgets(linebuf, sizeof(linebuf), f)) {
            char *p = linebuf;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p != 'C') continue;
            
            int target_id;
            int n = 0;
            if (sscanf(p, "C%d %n", &target_id, &n) < 1) continue;
            if (target_id != cid) continue;
            p += n;

            char cmd[32];
            n = 0;
            if (sscanf(p, " %31s %n", cmd, &n) < 1) continue;
            p += n;

            if (strcmp(cmd, "SLEEP") == 0) {
                int tm;
                if (sscanf(p, " %d", &tm) == 1) {
                    printf("Client %d: Sleeping for %dms\n", cid, tm);
                    msleep(tm);
                }
            } else if (strcmp(cmd, "UNDO") == 0) {
                request_t req = {REQ_UNDO, cid, 0, 0, "", 0, 0};
                response_t resp;
                printf("Client %d: Requesting UNDO\n", cid);
                if (send_request_and_wait_response(&req, &resp) != -1) {
                    if (resp.status == RESP_SUCCESS)
                        printf("Client %d: UNDO SUCCESS\n", cid);
                    else
                        printf("Client %d: UNDO FAILED/DROPPED\n", cid);
                }
            } else if (strcmp(cmd, "READ") == 0) {
                int l, pos;
                if (sscanf(p, " %d %d", &l, &pos) == 2) {
                    request_t req = {REQ_READ, cid, l, pos, "", 0, 0};
                    response_t resp;
                    printf("Client %d: Requesting READ lock for (%d,%d)\n", cid, l, pos);
                    if (send_request_and_wait_response(&req, &resp) != -1) {
                        if (resp.status == RESP_SUCCESS)
                            printf("Client %d: READ(%d,%d) SUCCESS - Value: '%s'\n", cid, l, pos, resp.word);
                        else
                            printf("Client %d: READ(%d,%d) DROPPED\n", cid, l, pos);
                    }
                }
            } else if (strcmp(cmd, "WRITE") == 0) {
                int l, pos, tm;
                char w[MAX_STRING_LEN + 1];
                if (sscanf(p, " %d %d %63s %d", &l, &pos, w, &tm) == 4) {
                    request_t req = {REQ_WRITE, cid, l, pos, "", tm, 0};
                    strncpy(req.word, w, MAX_STRING_LEN);
                    response_t resp;
                    printf("Client %d: Requesting WRITE lock for (%d,%d)\n", cid, l, pos);
                    if (send_request_and_wait_response(&req, &resp) != -1) {
                        if (resp.status == RESP_SUCCESS) {
                            printf("Client %d: WRITE(%d,%d) = '%s', sleeping for %dms\n", cid, l, pos, req.word, tm);
                            printf("Client %d: WRITE(%d,%d) COMPLETED\n", cid, l, pos);
                        } else {
                            printf("Client %d: WRITE(%d,%d) DROPPED\n", cid, l, pos);
                        }
                    }
                }
            }
            usleep(10000);
        }
        fclose(f);
    }

    sleep(3);
    running = 0;
    pthread_join(ptid, NULL);

    request_t req;
    memset(&req, 0, sizeof(req));
    req.req_type = REQ_SHUTDOWN;
    req.client_id = cid;
    mq_send(server_mqd, (const char*)&req, sizeof(request_t), 0);

    mq_close(client_mqd);
    mq_unlink(mqname);
    mq_close(server_mqd);
    return 0;
}
