/* agent_290.c - RemoteOps Agent (server)
 * IE3090 Network Programming - registration number IT24102290
 * DAY 3: PUT / GET (exact byte counting), UDP MONITOR START/STOP, QUIT.
 * Concurrency model : one POSIX thread per client connection
 * Control channel   : TCP, line-based protocol (brief section 2.3)
 * Monitoring channel: UDP datagrams, one extra thread per monitoring client*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*Personalised values(IT24102290) */
#define REG_NO        "IT24102290"
#define AGENT_PORT    9410                
#define SID           "0922"              
#define AUTH_TOKEN    "OPS-2290"
#define LOG_FILE      "remoteops_IT24102290.log"
#define STORE_DIR     "./agentfiles/IT24102290"

/*Tunables*/
#define MAX_FILE_SIZE    (10 * 1024 * 1024)  
#define MONITOR_INTERVAL 2                  
#define LINE_MAX_LEN     1024
#define RBUF_SIZE        8192
#define REPLY_MAX        20480
#define PROC_LIST_MAX    16000
#define MAX_FILENAME     100

/*Logging (thread-safe)*/
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

/* Per-client state */
typedef struct {
    int  fd;                        
    struct sockaddr_in addr;        
    char ip[INET_ADDRSTRLEN];
    int  authed;                    
    int  auth_fails;

    char   rbuf[RBUF_SIZE];        
    size_t rlen;

    /* UDP monitoring */
    int  mon_active;                
    int  mon_run;                  
    int  udp_fd;
    struct sockaddr_in udp_dest;
    pthread_t mon_tid;
    pthread_mutex_t mon_mx;
    pthread_cond_t  mon_cv;
} client_t;

/*Low-level send / receive helpers */
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

/*System statistics*/
static void build_sysinfo(char *out, size_t sz)
{
    double load = 0.0;
    long   mem_mb = 0, up = 0;
    FILE  *f;

    if ((f = fopen("/proc/loadavg", "r"))) {         
        if (fscanf(f, "%lf", &load) != 1) load = 0.0;
        fclose(f);
    }
    if ((f = fopen("/proc/uptime", "r"))) {
        double u;
        if (fscanf(f, "%lf", &u) == 1) up = (long)u;
        fclose(f);
    }
    if ((f = fopen("/proc/meminfo", "r"))) {
        char line[256], key[64];
        long val, total = 0, avail = -1, freem = 0, bufs = 0, cached = 0;
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "%63s %ld", key, &val) != 2) continue;
            if      (!strcmp(key, "MemTotal:"))     total  = val;
            else if (!strcmp(key, "MemAvailable:")) avail  = val;
            else if (!strcmp(key, "MemFree:"))      freem  = val;
            else if (!strcmp(key, "Buffers:"))      bufs   = val;
            else if (!strcmp(key, "Cached:"))       cached = val;
        }
        fclose(f);
        if (avail < 0) avail = freem + bufs + cached;  /* old kernels */
        mem_mb = (total - avail) / 1024;               /* kB -> MB */
    }
    snprintf(out, sz, "SYSINFO %.2f %ld %ld", load, mem_mb, up);
}
static void *monitor_thread(void *arg)
{
    client_t *c = (client_t *)arg;
    char stats[128], dgram[192];

    for (;;) {
        build_sysinfo(stats, sizeof stats);
        int n = snprintf(dgram, sizeof dgram, "%s SID:%s\n", stats, SID);
        sendto(c->udp_fd, dgram, (size_t)n, 0,
               (struct sockaddr *)&c->udp_dest, sizeof c->udp_dest);

                pthread_mutex_lock(&c->mon_mx);
        if (c->mon_run) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += MONITOR_INTERVAL;
            pthread_cond_timedwait(&c->mon_cv, &c->mon_mx, &ts);
        }
        int run = c->mon_run;
        pthread_mutex_unlock(&c->mon_mx);
        if (!run) break;
    }
    return NULL;
}

static void stop_monitor(client_t *c)
{
    if (!c->mon_active) return;
    pthread_mutex_lock(&c->mon_mx);
    c->mon_run = 0;
    pthread_cond_signal(&c->mon_cv);
    pthread_mutex_unlock(&c->mon_mx);
    pthread_join(c->mon_tid, NULL);
    close(c->udp_fd);
    c->mon_active = 0;
    log_event("MONITOR STOPPED for %s", c->ip);
}
static int valid_filename(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > MAX_FILENAME || s[0] == '.') return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-')
            return 0;
    return 1;
}

static int cmd_sysinfo(client_t *c)
{
    char stats[128];
    build_sysinfo(stats, sizeof stats);
    return reply(c, "OK %s", stats) < 0;
}

static int cmd_listproc(client_t *c)
{
    FILE *p = popen("ps -eo pid=,comm= 2>/dev/null", "r");
    if (!p) return reply(c, "ERR 013 INTERNAL_ERROR") < 0;

    char list[PROC_LIST_MAX + 1] = "";
    size_t used = 0;
    char line[256];
    while (fgets(line, sizeof line, p)) {
        int pid;
        char name[128];
        if (sscanf(line, " %d %127[^\n]", &pid, name) != 2) continue;
        for (char *q = name; *q; q++) if (*q == ' ' || *q == ',') *q = '_';
        char item[160];
        int len = snprintf(item, sizeof item, "%s%d:%s", used ? "," : "", pid, name);
        if (used + (size_t)len >= PROC_LIST_MAX) break;       /* truncate */
        memcpy(list + used, item, (size_t)len + 1);
        used += (size_t)len;
    }
    pclose(p);
    return reply(c, "OK PROCS %s", list) < 0;
}

static const struct { const char *name; const char *shell; } EXEC_TABLE[] = {
    { "DATE",     "date" },
    { "UPTIME",   "uptime" },
    { "DISKFREE", "df -h /" },
    { "HOSTNAME", "hostname" },
    { "WHOAMI",   "whoami" },
};

static int cmd_exec(client_t *c, char **save)
{
    char *name  = strtok_r(NULL, " ", save);
    if (!name) return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;
    char *extra = strtok_r(NULL, " ", save);

    const char *shell = NULL;
    if (!extra)
        for (size_t i = 0; i < sizeof EXEC_TABLE / sizeof EXEC_TABLE[0]; i++)
            if (strcmp(name, EXEC_TABLE[i].name) == 0) shell = EXEC_TABLE[i].shell;

    if (!shell) {
        log_event("EXEC REJECTED '%s' from %s", name, c->ip);
        return reply(c, "ERR 002 COMMAND_NOT_ALLOWED") < 0;
    }

    FILE *p = popen(shell, "r");
    if (!p) return reply(c, "ERR 013 INTERNAL_ERROR") < 0;
    char out[1024];
    size_t n = fread(out, 1, sizeof out - 1, p);
    pclose(p);
    out[n] = '\0';

    for (size_t i = 0; i < n; i++)                
        if (out[i] == '\n' || out[i] == '\r' || out[i] == '\t') out[i] = ' ';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
    if (n == 0) strcpy(out, "(no output)");

    log_event("EXEC %s from %s", name, c->ip);
    return reply(c, "OK EXEC_RESULT %s", out) < 0;
}

static int recv_exact(client_t *c, FILE *fp, unsigned long long size)
{
    unsigned long long left = size;
    int werr = 0;

    if (c->rlen > 0 && left > 0) {
        size_t take = c->rlen < left ? c->rlen : (size_t)left;
        if (fp && fwrite(c->rbuf, 1, take, fp) != take) werr = 1;
        memmove(c->rbuf, c->rbuf + take, c->rlen - take);
        c->rlen -= take;
        left    -= take;
    }
    char buf[8192];
    while (left > 0) {
        size_t want = left < sizeof buf ? (size_t)left : sizeof buf;
        ssize_t n = recv(c->fd, buf, want, 0);
        if (n == 0) return -1;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (fp && !werr && fwrite(buf, 1, (size_t)n, fp) != (size_t)n) werr = 1;
        left -= (unsigned long long)n;
    }
    return werr ? -2 : 0;
}

static int cmd_put(client_t *c, char **save)
{
    char *name    = strtok_r(NULL, " ", save);
    char *sizestr = name ? strtok_r(NULL, " ", save) : NULL;
    char *extra   = sizestr ? strtok_r(NULL, " ", save) : NULL;
    if (!name || !sizestr || extra || !isdigit((unsigned char)sizestr[0]))
        return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;

    char *end;
    errno = 0;
    unsigned long long size = strtoull(sizestr, &end, 10);
    if (*end || errno) return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;

    if (size > MAX_FILE_SIZE) {
        log_event("PUT REJECTED %s (%llu bytes, too large) from %s", name, size, c->ip);
        reply(c, "ERR 004 FILE_TOO_LARGE");
        return 1;
    }

    char final_path[512], tmp_path[512];
    FILE *fp = NULL;
    if (valid_filename(name)) {
        snprintf(final_path, sizeof final_path, "%s/%s", STORE_DIR, name);
        snprintf(tmp_path, sizeof tmp_path, "%s/.part_%d_%s", STORE_DIR, c->fd, name);
        fp = fopen(tmp_path, "wb");
    }

    if (!fp) {                                    
        int r = recv_exact(c, NULL, size);        
        if (r == -1) return 1;
        if (!valid_filename(name)) return reply(c, "ERR 010 INVALID_FILENAME") < 0;
        return reply(c, "ERR 013 INTERNAL_ERROR") < 0;
    }

    int r = recv_exact(c, fp, size);
    if (fclose(fp) != 0 && r == 0) r = -2;

    if (r == -1) {                                
        unlink(tmp_path);
        log_event("PUT ABORTED %s (client disconnected) from %s", name, c->ip);
        return 1;
    }
    if (r == -2 || rename(tmp_path, final_path) != 0) {
        unlink(tmp_path);
        return reply(c, "ERR 013 INTERNAL_ERROR") < 0;
    }

    log_event("PUT %s %llu bytes from %s", name, size, c->ip);
    return reply(c, "OK FILE_RECEIVED %s", name) < 0;
}

static int cmd_get(client_t *c, char **save)
{
    char *name  = strtok_r(NULL, " ", save);
    char *extra = name ? strtok_r(NULL, " ", save) : NULL;
    if (!name || extra) return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;

    struct stat st;
    char path[512];
    FILE *fp = NULL;
    if (valid_filename(name)) {
        snprintf(path, sizeof path, "%s/%s", STORE_DIR, name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) fp = fopen(path, "rb");
    }
    if (!fp) {
        log_event("GET %s NOT FOUND from %s", name, c->ip);
        return reply(c, "ERR 005 FILE_NOT_FOUND") < 0;
    }

    unsigned long long size = (unsigned long long)st.st_size, sent = 0;
    if (reply(c, "OK FILE_SEND %s %llu", name, size) < 0) { fclose(fp); return 1; }

    char buf[8192];
    while (sent < size) {
        size_t want = (size - sent) < sizeof buf ? (size_t)(size - sent) : sizeof buf;
        size_t n = fread(buf, 1, want, fp);
        if (n == 0 || send_all(c->fd, buf, n) < 0) {
            fclose(fp);
            return 1;          
        }
        sent += n;
    }
    fclose(fp);
    log_event("GET %s %llu bytes to %s", name, size, c->ip);
    return 0;
}

static int cmd_monitor(client_t *c, char **save)
{
    char *sub = strtok_r(NULL, " ", save);
    if (!sub) return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;

    if (strcmp(sub, "START") == 0) {
        char *ps    = strtok_r(NULL, " ", save);
        char *extra = ps ? strtok_r(NULL, " ", save) : NULL;
        if (!ps || extra || !isdigit((unsigned char)ps[0]))
            return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;
        char *end;
        long port = strtol(ps, &end, 10);
        if (*end || port < 1 || port > 65535)
            return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;
        if (c->mon_active) return reply(c, "ERR 011 ALREADY_MONITORING") < 0;

        c->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (c->udp_fd < 0) return reply(c, "ERR 013 INTERNAL_ERROR") < 0;
        c->udp_dest = c->addr;                       /* Controller's IP ... */
        c->udp_dest.sin_port = htons((uint16_t)port); /* ... on its UDP port */
        c->mon_run = 1;
        if (pthread_create(&c->mon_tid, NULL, monitor_thread, c) != 0) {
            close(c->udp_fd);
            return reply(c, "ERR 013 INTERNAL_ERROR") < 0;
        }
        c->mon_active = 1;
        log_event("MONITOR STARTED for %s udp_port=%ld", c->ip, port);
        return reply(c, "OK MONITOR_STARTED") < 0;
    }

    if (strcmp(sub, "STOP") == 0) {
        if (!c->mon_active) return reply(c, "ERR 012 NOT_MONITORING") < 0;
        stop_monitor(c);
        return reply(c, "OK MONITOR_STOPPED") < 0;
    }
    return reply(c, "ERR 009 BAD_ARGUMENTS") < 0;
}

/*Command dispatcher*/
static int handle_command(client_t *c, char *line)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " ", &save);
    if (!cmd) return reply(c, "ERR 007 EMPTY_COMMAND") < 0;

    log_event("CMD %s from %s", cmd, c->ip);       

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

    if (strcmp(cmd, "SYSINFO")  == 0) return cmd_sysinfo(c);
    if (strcmp(cmd, "LISTPROC") == 0) return cmd_listproc(c);
    if (strcmp(cmd, "EXEC")     == 0) return cmd_exec(c, &save);
    if (strcmp(cmd, "PUT")      == 0) return cmd_put(c, &save);
    if (strcmp(cmd, "GET")      == 0) return cmd_get(c, &save);
    if (strcmp(cmd, "MONITOR")  == 0) return cmd_monitor(c, &save);
    if (strcmp(cmd, "QUIT")     == 0) {
        stop_monitor(c);
        reply(c, "OK BYE");
        return 1;
    }
    return reply(c, "ERR 006 UNKNOWN_COMMAND") < 0;
}

/*One thread per client*/
static void *client_thread(void *arg)
{
    client_t *c = (client_t *)arg;
    inet_ntop(AF_INET, &c->addr.sin_addr, c->ip, sizeof c->ip);
    pthread_mutex_init(&c->mon_mx, NULL);
    pthread_cond_init(&c->mon_cv, NULL);
    log_event("CONNECT %s:%d", c->ip, ntohs(c->addr.sin_port));

    char line[LINE_MAX_LEN];
    for (;;) {
        int n = read_line(c, line, sizeof line);
        if (n == -2) { reply(c, "ERR 008 LINE_TOO_LONG"); break; }
        if (n < 0)   break;                       
        if (handle_command(c, line)) break;
    }

    stop_monitor(c);                               
    close(c->fd);
    log_event("DISCONNECT %s:%d", c->ip, ntohs(c->addr.sin_port));
    pthread_mutex_destroy(&c->mon_mx);
    pthread_cond_destroy(&c->mon_cv);
    free(c);
    return NULL;
}

/*main*/
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
