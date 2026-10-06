/* controller_290.c - RemoteOps Controller (client)
 * IE3090 Network Programming - registration number IT24102290
 * DAY 4 (part 1): TCP connection, automatic AUTH, line framing, SYSINFO,
 * LISTPROC, EXEC, raw protocol lines, QUIT.
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

/*Connection state*/
static int    sock = -1;                   
static char   rbuf[RBUF_SIZE];            
static size_t rlen = 0;

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


static void print_help(void)
{
    printf("Commands (case-insensitive):\n");
    printf("  sysinfo         CPU load, memory used (MB), uptime\n");
    printf("  listproc [full] list running processes\n");
    printf("  exec <NAME>     DATE, UPTIME, DISKFREE, HOSTNAME or WHOAMI\n");
    printf("  raw <line>  send one protocol line exactly as typed\n");
    printf("  help show this list\n");
    printf("  quit disconnect\n");
}

/* main  */
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
        printf("Unknown command '%s'. Type 'help'.\n", c);
    }

    printf("> QUIT\n");
    if (request("QUIT", resp, sizeof resp) == 0) print_reply(resp, 1);
    close(sock);
    printf("Disconnected.\n");
    return 0;

lost:
    close(sock);
    printf("Connection lost. Exiting.\n");
    return 1;
}
