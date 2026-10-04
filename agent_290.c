/* agent_290.c - RemoteOps Agent (server)
 * IE3090 Network Programming - registration number IT24102290
 * DAY 2 (part 1): line framing, reply() with SID tag, AUTH.
 */
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
#define AGENT_PORT    9410               
#define SID           "0922"               
#define AUTH_TOKEN    "OPS-2290"
#define LOG_FILE      "remoteops_IT24102290.log"
#define STORE_DIR     "./agentfiles/IT24102290"
#define MAX_FILE_SIZE (10 * 1024 * 1024)   


#define LINE_MAX_LEN  1024                 
#define RBUF_SIZE     8192                
#define REPLY_MAX     20480                



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
    int  fd;                       
    struct sockaddr_in addr;       
    char ip[INET_ADDRSTRLEN];       
    int  authed;                        
    int  auth_fails;               
    char   rbuf[RBUF_SIZE];         
    size_t rlen;                    
} client_t;




static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        p   += n;
        len -= (size_t)n;
    }
    return 0;
}


static int reply(client_t *c, const char *fmt, ...)
{
    char body[REPLY_MAX], line[REPLY_MAX + 32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    int n = snprintf(line, sizeof line, "%s SID:%s\n", body, SID);
    return send_all(c->fd, line, (size_t)n);
}


static int read_line(client_t *c, char *out, size_t outsz)
{
    for (;;) {
       
        char *nl = memchr(c->rbuf, '\n', c->rlen);
        if (nl) {
            size_t consumed = (size_t)(nl - c->rbuf) + 1;   
            size_t linelen  = consumed - 1;
            if (linelen >= outsz) return -2;
            memcpy(out, c->rbuf, linelen);
            if (linelen > 0 && out[linelen - 1] == '\r') linelen--;  
            out[linelen] = '\0';
           
            memmove(c->rbuf, c->rbuf + consumed, c->rlen - consumed);
            c->rlen -= consumed;
            return (int)linelen;
        }
        if (c->rlen == RBUF_SIZE) return -2;

        
        ssize_t n = recv(c->fd, c->rbuf + c->rlen, RBUF_SIZE - c->rlen, 0);
        if (n == 0) return -1;                             
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        c->rlen += (size_t)n;
    }
}



static int handle_command(client_t *c, char *line)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " ", &save);
    if (!cmd) return reply(c, "ERR 007 EMPTY_COMMAND") < 0;

    log_event("CMD %s from %s", cmd, c->ip);       /* never log the token itself */

    if (strcmp(cmd, "AUTH") == 0) {
        char *tok = strtok_r(NULL, " ", &save);
        if (tok && strcmp(tok, AUTH_TOKEN) == 0) {
            c->authed = 1;
            log_event("AUTH OK %s", c->ip);
            return reply(c, "OK AUTHENTICATED") < 0;
        }
        c->auth_fails++;
        log_event("AUTH FAILED %s (attempt %d)", c->ip, c->auth_fails);
        if (reply(c, "ERR 001 AUTH_FAILED") < 0) return 1;
        return c->auth_fails >= 3;                 
    }

   
    if (!c->authed) return reply(c, "ERR 003 NOT_AUTHENTICATED") < 0;

    if (strcmp(cmd, "QUIT") == 0) {
        reply(c, "OK BYE");
        return 1;
    }

       return reply(c, "ERR 006 UNKNOWN_COMMAND") < 0;
}


static void *client_thread(void *arg)
{
    client_t *c = (client_t *)arg;
    inet_ntop(AF_INET, &c->addr.sin_addr, c->ip, sizeof c->ip);
    log_event("CONNECT %s:%d", c->ip, ntohs(c->addr.sin_port));

    char line[LINE_MAX_LEN];
    for (;;) {
        int n = read_line(c, line, sizeof line);
        if (n == -2) { reply(c, "ERR 008 LINE_TOO_LONG"); break; }
        if (n < 0)   break;                        /* disconnect*/
        if (handle_command(c, line)) break;
    }

    close(c->fd);
    log_event("DISCONNECT %s:%d", c->ip, ntohs(c->addr.sin_port));
    free(c);
    return NULL;
}


int main(void)
{
    signal(SIGPIPE, SIG_IGN);     
    mkdir("./agentfiles", 0755);
    mkdir(STORE_DIR, 0755);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);     
    if (lfd < 0) { perror("socket"); return 1; }

    int yes = 1;                                   
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);        
    sa.sin_port        = htons(AGENT_PORT);      

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
        pthread_detach(tid);       
    }
}
