#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define REG_NO        "IT24102290"
#define AGENT_PORT    9410                 /* 7000 + 2410 */
#define SID           "0922"               /* last 4 digits "2290" reversed */
#define AUTH_TOKEN    "OPS-2290"
#define LOG_FILE      "remoteops_IT24102290.log"
#define STORE_DIR     "./agentfiles/IT24102290"
#define MAX_FILE_SIZE (10 * 1024 * 1024)   /* 10 MB, used for ERR 004 later */


/* Many client threads log at once, so a mutex stops their lines mixing. */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void log_event(const char *fmt, ...)
{
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    pthread_mutex_lock(&log_mutex);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "[%s] ", ts);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    pthread_mutex_unlock(&log_mutex);
}


typedef struct {
    int  fd;                        /* TCP socket for this client */
    struct sockaddr_in addr;        /* client's address */
    char ip[INET_ADDRSTRLEN];       /* client's IP as text */
    int  authed;                    
} client_t;


static void *client_thread(void *arg)
{
    client_t *c = (client_t *)arg;
    inet_ntop(AF_INET, &c->addr.sin_addr, c->ip, sizeof c->ip);
    log_event("CONNECT %s:%d", c->ip, ntohs(c->addr.sin_port));

    const char *hello = "OK HELLO SID:" SID "\n";
    send(c->fd, hello, strlen(hello), MSG_NOSIGNAL);

    /* Day 1: just wait until the client disconnects (graceful or not). */
    char buf[256];
    for (;;) {
        ssize_t n = recv(c->fd, buf, sizeof buf, 0);
        if (n == 0) break;                         /* client closed */
        if (n < 0) { if (errno == EINTR) continue; break; }
    }

    close(c->fd);
    log_event("DISCONNECT %s:%d", c->ip, ntohs(c->addr.sin_port));
    free(c);
    return NULL;
}


int main(void)
{
    signal(SIGPIPE, SIG_IGN);       /* writing to a dead client must not kill us */
    mkdir("./agentfiles", 0755);
    mkdir(STORE_DIR, 0755);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);     /* IPv4 + TCP */
    if (lfd < 0) { perror("socket"); return 1; }

    int yes = 1;                                   /* allow quick restarts */
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);        /* all network interfaces */
    sa.sin_port        = htons(AGENT_PORT);        /* 9410 */

    if (bind(lfd, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("bind"); return 1; }
    if (listen(lfd, 16) < 0) { perror("listen"); return 1; }

    printf("RemoteOps Agent (%s) listening on port %d\n", REG_NO, AGENT_PORT);
    log_event("AGENT STARTED port=%d", AGENT_PORT);

    for (;;) {
        client_t *c = calloc(1, sizeof *c);
        if (!c) { perror("calloc"); continue; }
        socklen_t len = sizeof c->addr;
        c->fd = accept(lfd, (struct sockaddr *)&c->addr, &len);
        if (c->fd < 0) {
            free(c);
            if (errno != EINTR) perror("accept");
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, c) != 0) {
            close(c->fd);
            free(c);
            continue;
        }
        pthread_detach(tid);        /* thread cleans up after itself */
    }
}
