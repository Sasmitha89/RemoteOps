
/* controller_290.c - RemoteOps Controller (client)
 * IE3090 Network Programming - registration number IT24102290
 * DAY 4 (part 2): PUT / GET with exact byte counting and throughput report,
 *                 UDP receiver thread for MONITOR START / STOP.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
#include <netdb.h>

/*Personalised values (derived from IT24102290)*/
#define DEFAULT_PORT  9410                 
#define SID           "0922"               
#define AUTH_TOKEN    "OPS-2290"

#define RBUF_SIZE     65536                
#define LINE_BUF      32768
#define MAX_CMD_LEN   1000                 
#define MAX_FILE_SIZE (10 * 1024 * 1024)  
#define DEFAULT_UDP_PORT 5000

/*Connection state*/
static int    sock = -1;                   
static char   rbuf[RBUF_SIZE];            
static size_t rlen = 0;
static int       udp_fd = -1;             
static int       udp_active = 0;
static volatile int udp_run = 0;
static pthread_t udp_tid;

/*Low-level helpers*/

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

static int send_line(const char *cmd)
{
    char line[MAX_CMD_LEN + 8];
    int n = snprintf(line, sizeof line, "%s\n", cmd);
    if (n < 0 || (size_t)n >= sizeof line) return -1;
    return send_all(sock, line, (size_t)n);
}

static int read_line(char *out, size_t outsz)
{
    for (;;) {
        char *nl = memchr(rbuf, '\n', rlen);
        if (nl) {
            size_t consumed = (size_t)(nl - rbuf) + 1;
            size_t linelen  = consumed - 1;
            if (linelen >= outsz) return -2;
            memcpy(out, rbuf, linelen);
            if (linelen > 0 && out[linelen - 1] == '\r') linelen--;
            out[linelen] = '\0';
            memmove(rbuf, rbuf + consumed, rlen - consumed);
            rlen -= consumed;
            return (int)linelen;
        }
        if (rlen == RBUF_SIZE) return -2;
        ssize_t n = recv(sock, rbuf + rlen, RBUF_SIZE - rlen, 0);
        if (n == 0) return -1;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        rlen += (size_t)n;
    }
}

static int read_exact(FILE *fp, unsigned long long size)
{
    unsigned long long left = size;
    int werr = 0;
    if (rlen > 0 && left > 0) {
        size_t take = rlen < left ? rlen : (size_t)left;
        if (fp && fwrite(rbuf, 1, take, fp) != take) werr = 1;
        memmove(rbuf, rbuf + take, rlen - take);
        rlen -= take;
        left -= take;
    }
    char buf[8192];
    while (left > 0) {
        size_t want = left < sizeof buf ? (size_t)left : sizeof buf;
        ssize_t n = recv(sock, buf, want, 0);
        if (n == 0) return -1;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (fp && !werr && fwrite(buf, 1, (size_t)n, fp) != (size_t)n) werr = 1;
        left -= (unsigned long long)n;
    }
    return werr ? -2 : 0;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void show_throughput(const char *what, unsigned long long bytes, double secs)
{
    if (secs < 1e-6) secs = 1e-6;
    printf("[throughput] %s %llu bytes in %.3f s = %.0f bytes/s (%.2f KB/s)\n",
           what, bytes, secs, (double)bytes / secs, (double)bytes / secs / 1024.0);
}

/* Print a response; verify it carries our SID tag. Long lines (LISTPROC)
 * are shortened for display unless `full` is set. */
static void print_reply(const char *line, int full)
{
    size_t n = strlen(line);
    if (!full && n > 200) {
        const char *tag = strrchr(line, ' ');
        printf("%.150s ... [%zu characters, use 'listproc full' to show all] %s\n",
               line, n, tag ? tag + 1 : "");
    } else {
        puts(line);
    }
    const char *want = " SID:" SID;
    size_t wl = strlen(want);
    if (n < wl || strcmp(line + n - wl, want) != 0)
        printf("  (warning: response does not end with SID:%s)\n", SID);
}

static int request(const char *cmd, char *resp, size_t sz)
{
    if (send_line(cmd) < 0) { printf("Send failed: connection lost.\n"); return -1; }
    int n = read_line(resp, sz);
    if (n == -1) { printf("Connection closed by the Agent.\n"); return -1; }
    if (n == -2) { printf("Response too long.\n"); return -1; }
    return 0;
}

static int simple_cmd(const char *cmd, int full)
{
    static char resp[LINE_BUF];
    if (strlen(cmd) > MAX_CMD_LEN) { printf("Command too long.\n"); return 0; }
    printf("> %s\n", cmd);
    if (request(cmd, resp, sizeof resp) < 0) return -1;
    print_reply(resp, full);
    return 0;
}

static int tcp_connect(const char *host, int port)
{
    struct addrinfo hints, *res, *ai;
    char ps[16];
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0) return -1;
    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/*UDP monitor receiver*/
static void *udp_thread(void *arg)
{
    (void)arg;
    char buf[512], ip[INET_ADDRSTRLEN];
    while (udp_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(udp_fd, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            break;
        }
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
        buf[n] = '\0';
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        printf("\r[UDP from %s] %s\nremoteops> ", ip, buf);
        fflush(stdout);
    }
    return NULL;
}

static int start_udp(int port)
{
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("udp socket"); return -1; }
    int yes = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct timeval tv = { 1, 0 };                 
    setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons((uint16_t)port);
    if (bind(udp_fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        perror("udp bind"); close(udp_fd); udp_fd = -1; return -1;
    }
    udp_run = 1;
    if (pthread_create(&udp_tid, NULL, udp_thread, NULL) != 0) {
        close(udp_fd); udp_fd = -1; udp_run = 0; return -1;
    }
    udp_active = 1;
    return 0;
}

static void stop_udp(void)
{
    if (!udp_active) return;
    udp_run = 0;
    pthread_join(udp_tid, NULL);
    close(udp_fd);
    udp_fd = -1;
    udp_active = 0;
}

/*PUT / GET / MONITOR */
static char *skip_ws(char *s) { while (s && *s && isspace((unsigned char)*s)) s++; return s; }

static int do_put(char *args)
{
    char *save = NULL;
    char *local  = strtok_r(args, " \t", &save);
    char *remote = strtok_r(NULL, " \t", &save);
    if (!local) { printf("Usage: put <local_file> [remote_name]\n"); return 0; }

    struct stat st;
    if (stat(local, &st) != 0 || !S_ISREG(st.st_mode)) { printf("Cannot read local file '%s'.\n", local); return 0; }
    unsigned long long size = (unsigned long long)st.st_size;
    if (size > MAX_FILE_SIZE) { printf("File is larger than the 10 MB limit; not sent.\n"); return 0; }
    FILE *fp = fopen(local, "rb");
    if (!fp) { perror("fopen"); return 0; }

    const char *base = strrchr(local, '/');
    base = base ? base + 1 : local;
    if (!remote) remote = (char *)base;

    char cmd[512], resp[LINE_BUF];
    snprintf(cmd, sizeof cmd, "PUT %s %llu", remote, size);
    printf("> %s   (followed by %llu raw bytes)\n", cmd, size);

    double t0 = now_s();
    if (send_line(cmd) < 0) { fclose(fp); printf("Send failed: connection lost.\n"); return -1; }
    char buf[8192];
    unsigned long long sent = 0;
    while (sent < size) {
        size_t n = fread(buf, 1, sizeof buf, fp);
        if (n == 0 || send_all(sock, buf, n) < 0) { fclose(fp); printf("Upload failed: connection lost.\n"); return -1; }
        sent += n;
    }
    fclose(fp);
    int n = read_line(resp, sizeof resp);
    double t1 = now_s();
    if (n < 0) { printf("Connection closed by the Agent.\n"); return -1; }
    print_reply(resp, 1);
    if (strncmp(resp, "OK FILE_RECEIVED", 16) == 0) show_throughput("uploaded", size, t1 - t0);
    return 0;
}

static int do_get(char *args)
{
    char *save = NULL;
    char *remote = strtok_r(args, " \t", &save);
    char *local  = strtok_r(NULL, " \t", &save);
    if (!remote) { printf("Usage: get <remote_name> [local_file]\n"); return 0; }
    if (!local) { local = strrchr(remote, '/'); local = local ? local + 1 : remote; }

    char cmd[512], resp[LINE_BUF];
    snprintf(cmd, sizeof cmd, "GET %s", remote);
    printf("> %s\n", cmd);
    double t0 = now_s();
    if (request(cmd, resp, sizeof resp) < 0) return -1;
    if (strncmp(resp, "OK FILE_SEND ", 13) != 0) { print_reply(resp, 1); return 0; }   /* ERR ... */

    char rname[256];
    unsigned long long size;
    if (sscanf(resp + 13, "%255s %llu", rname, &size) != 2) { printf("Malformed response: %s\n", resp); return -1; }
    print_reply(resp, 1);

    FILE *fp = fopen(local, "wb");
    if (!fp) perror("fopen");                      
    int r = read_exact(fp, size);
    if (fp && fclose(fp) != 0 && r == 0) r = -2;
    double t1 = now_s();
    if (r == -1) { printf("Download interrupted: connection lost.\n"); return -1; }
    if (r == -2 || !fp) { printf("Could not write local file '%s'.\n", local); return 0; }
    printf("Saved %llu bytes to '%s'\n", size, local);
    show_throughput("downloaded", size, t1 - t0);
    return 0;
}

static int do_monitor(char *args)
{
    char *save = NULL;
    char *sub = strtok_r(args, " \t", &save);
    char cmd[64], resp[LINE_BUF];
    if (!sub) { printf("Usage: monitor start [udp_port] | monitor stop\n"); return 0; }

    if (strcasecmp(sub, "start") == 0) {
        char *ps = strtok_r(NULL, " \t", &save);
        int port = ps ? atoi(ps) : DEFAULT_UDP_PORT;
        if (port < 1 || port > 65535) { printf("Invalid UDP port.\n"); return 0; }
        if (udp_active) { printf("Already listening for monitor data.\n"); return 0; }
        if (start_udp(port) < 0) return 0;          /* bind BEFORE asking the Agent to send */
        snprintf(cmd, sizeof cmd, "MONITOR START %d", port);
        printf("> %s   (listening for UDP on port %d)\n", cmd, port);
        if (request(cmd, resp, sizeof resp) < 0) return -1;
        print_reply(resp, 1);
        if (strncmp(resp, "OK MONITOR_STARTED", 18) != 0) stop_udp();
        return 0;
    }
    if (strcasecmp(sub, "stop") == 0) {
        printf("> MONITOR STOP\n");
        if (request("MONITOR STOP", resp, sizeof resp) < 0) return -1;
        print_reply(resp, 1);
        if (strncmp(resp, "OK MONITOR_STOPPED", 18) == 0) stop_udp();
        return 0;
    }
    printf("Usage: monitor start [udp_port] | monitor stop\n");
    return 0;
}

static void print_help(void)
{
    printf("Commands (case-insensitive):\n");
    printf("  sysinfo                      CPU load, memory used (MB), uptime\n");
    printf("  listproc [full]              list running processes\n");
    printf("  exec <NAME>                  DATE, UPTIME, DISKFREE, HOSTNAME or WHOAMI\n");
    printf("  put <local> [remote]         upload a file (max 10 MB)\n");
    printf("  get <remote> [local]         download a file\n");
    printf("  monitor start [udp_port]     start the UDP monitoring stream (default %d)\n", DEFAULT_UDP_PORT);
    printf("  monitor stop                 stop the stream\n");
    printf("  raw <line>                   send one protocol line exactly as typed\n");
    printf("  help                         show this list\n");
    printf("  quit                         disconnect\n");
}

/* main*/
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <agent_ip_or_host> [port] [token]\n", argv[0]);
        return 1;
    }
    const char *host  = argv[1];
    int         port  = argc > 2 ? atoi(argv[2]) : DEFAULT_PORT;
    const char *token = argc > 3 ? argv[3] : AUTH_TOKEN;
    signal(SIGPIPE, SIG_IGN);

    sock = tcp_connect(host, port);
    if (sock < 0) { fprintf(stderr, "Cannot connect to %s:%d\n", host, port); return 1; }
    printf("Connected to %s:%d\n", host, port);

    char resp[LINE_BUF], cmd[MAX_CMD_LEN + 8];
    snprintf(cmd, sizeof cmd, "AUTH %s", token);         
    printf("> %s\n", cmd);
    if (request(cmd, resp, sizeof resp) < 0) { close(sock); return 1; }
    print_reply(resp, 1);
    if (strncmp(resp, "OK AUTHENTICATED", 16) != 0) {
        printf("Authentication failed. Exiting.\n");
        close(sock);
        return 1;
    }
    printf("Type 'help' for commands.\n");

    char input[2048];
    for (;;) {
        printf("remoteops> ");
        fflush(stdout);
        if (!fgets(input, sizeof input, stdin)) { printf("\n"); break; }  
        input[strcspn(input, "\r\n")] = '\0';
        char *save = NULL;
        char *c = strtok_r(input, " \t", &save);
        if (!c) continue;
        char *rest = save ? save : "";
        while (*rest == ' ' || *rest == '\t') rest++;

        if (!strcasecmp(c, "quit") || !strcasecmp(c, "exit")) break;
        if (!strcasecmp(c, "help") || !strcmp(c, "?")) { print_help(); continue; }
        if (!strcasecmp(c, "sysinfo")) { if (simple_cmd("SYSINFO", 0) < 0) goto lost; continue; }
        if (!strcasecmp(c, "listproc")) {
            if (simple_cmd("LISTPROC", strcasecmp(rest, "full") == 0) < 0) goto lost;
            continue;
        }
        if (!strcasecmp(c, "exec")) {
            if (!*rest) { printf("Usage: exec <NAME>\n"); continue; }
            snprintf(cmd, sizeof cmd, "EXEC %s", rest);
            if (simple_cmd(cmd, 1) < 0) goto lost;
            continue;
        }
        if (!strcasecmp(c, "raw")) {
            if (!*rest) { printf("Usage: raw <protocol line>\n"); continue; }
            if (simple_cmd(rest, 1) < 0) goto lost;
            continue;
        }
        if (!strcasecmp(c, "put"))     { if (do_put(skip_ws(rest)) < 0) goto lost; continue; }
        if (!strcasecmp(c, "get"))     { if (do_get(skip_ws(rest)) < 0) goto lost; continue; }
        if (!strcasecmp(c, "monitor")) { if (do_monitor(skip_ws(rest)) < 0) goto lost; continue; }
        printf("Unknown command '%s'. Type 'help'.\n", c);
    }

    stop_udp();
    printf("> QUIT\n");
    if (request("QUIT", resp, sizeof resp) == 0) print_reply(resp, 1);
    close(sock);
    printf("Disconnected.\n");
    return 0;

lost:
    stop_udp();
    close(sock);
    printf("Connection lost. Exiting.\n");
    return 1;
}
