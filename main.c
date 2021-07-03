#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#define QUOTES "quotes"
#define IRC_CHANNEL "##politics"
#define SERVER "irc.libera.chat"
#define RANDQUOTE_SIZE 1024
#define NICKNAME_SIZE 64
#define INTERVAL 45

struct context {
    time_t last_quote;
    int print_no;
    FILE *global;
    FILE *channel;
};

static char* find_nickname (char *input) {
    while (*input && *input != '<')
        input++;
    return input;
}

static int copy_nickname (char *input, char *output) {
    char *ptr = find_nickname (input);
    if (!ptr || !*ptr)
        return 0;
    ptr++;
    while (*ptr != '>')
        *output++ = *ptr++;
    *output = 0;
    return 1;
}

static void get_timestamp (char *msg, char *number) {
    int i = 0;
    while (*msg <= '9' && *msg >= '0')
        number[i++] = *msg++;
}

static char* find_message (char *input) {
    input = strchr (input, '>');
    if (input && *input && input[1])
        return &input[2];
    else
        return NULL;
}

static int sflush (char *s) {
    while (*s != 0 && *s != '\n')
        s++;
    if (!*s)
        return 0;
    else {
        *s = 0;
        return 1;
    }
}

static int strstart(char *a, char *b) {
    size_t la = strlen(a) - 1, lb = strlen(b) - 1;
    return la >= lb && !strncmp(a, b, lb);
}

static void addquote (FILE *out, char *msg, char *quote) {
    if (strlen (quote) > 0) {
        char nickname[NICKNAME_SIZE] = {0};
        char timestamp[32] = {0};
        copy_nickname (msg, nickname);
        get_timestamp (msg, timestamp);
        FILE *q = fopen (QUOTES, "a");
        fprintf (q, "%s;%s;%s\n", timestamp, nickname, quote);
        fclose (q);
        fprintf (out, "/PRIVMSG %s :added quote: %s\n", nickname, quote);
        fflush (out);
    }
}

static int count_lines (FILE *q) {
    int count = 0, c;
    while ((c = fgetc (q)) != EOF)
        if (c == '\n') count++;
    rewind (q);
    return count;
}

static int randrange (int a, int b) {
    float r = (float)rand () / RAND_MAX;
    r *= (b - a);
    r += a;
    return r + 0.5;
}

static time_t extract_timestamp(char *str) {
    char number[32] = {0};
    get_timestamp (str, number);
    return (time_t)strtol(number, NULL, 10);
}

static void extract_nickname(char *str, char nickname[NICKNAME_SIZE]) {
    int i = 0;
    while (*str++ != ';');
    while (*str != ';')
        nickname[i++] = *str++;
}

static char* extract_quote (char *str) {
    for (int i = 0; i < 2; i++) {
        while (*str != ';' && *str != 0) str++;
        if (*str != 0)
            str++;
    }
    return str;
}

static void printquote (FILE *out, FILE *q, int n) {
    char buffer[RANDQUOTE_SIZE] = {0};
    char nickname[NICKNAME_SIZE] = {0};
    int count = 0, c;
    while (count < n && (c = fgetc (q)) != EOF)
        if (c == '\n') count++;
    if (fgets (buffer, sizeof buffer, q)) {
        sflush(buffer);
        /* time_t ts = extract_timestamp (buffer); */
        extract_nickname (buffer, nickname);
        char *quote = extract_quote (buffer);
        fprintf (out, "quote %d by %s: %s\n", n + 1, nickname, quote);
        fflush (out);
    } else {
        printf ("ferror : %d\n", ferror(q));
        printf ("feof : %d\n", feof(q));
        printf ("woops\n");
    }
}

static void noquotes (struct context *ctx) {
    fprintf (ctx->channel, "no quotes yet\n");
    fflush (ctx->channel);
}

static void randquote (struct context *ctx, char *msg) {
    if (time (NULL) - ctx->last_quote < INTERVAL) {
        if (ctx->print_no) {
            /* char nickname[NICKNAME_SIZE] = {0}; */
            /* copy_nickname (msg, nickname); */
            /* fprintf (ctx->channel, "%s: no!\n", nickname); */
            /* fflush (ctx->channel); */
            ctx->print_no = 0;
        }
    } else {
        FILE *q = NULL;
        if (q = fopen (QUOTES, "r")) {
            int lines = count_lines (q);
            if (lines > 0) {
                int r = randrange (0, lines - 1);
                printquote (ctx->channel, q, r);
            } else {
                noquotes (ctx);
            }
            fclose (q);
        } else {
            noquotes (ctx);
        }
        ctx->last_quote = time (NULL);
        ctx->print_no = 1;
    }
}

static void lastquote (struct context *ctx) {
    if (time (NULL) - ctx->last_quote >= INTERVAL) {
        FILE *q = NULL;
        if (q = fopen (QUOTES, "r")) {
            int lines = count_lines (q);
            if (lines > 0) {
                printquote (ctx->channel, q, lines - 1);
            } else {
                noquotes (ctx);
            }
            fclose (q);
        } else {
            noquotes (ctx);
        }
        ctx->last_quote = time (NULL);
    }
}

static void run_cmd (struct context *ctx, char *msg, char *cmd) {
    if (strstart (cmd, "!addquote")) {
        addquote (ctx->global, msg, &cmd[strlen("!addquote") + 1]);
    } else if (strstart (cmd, "!randquote")) {
        randquote (ctx, msg);
    } else if (strstart (cmd, "!lastquote")) {
        lastquote (ctx);
    }
}

int main (int argc, char **argv)
{
    char buffer[1024];
    char channel[256], in_path[256], out_path[256];
    FILE *in = NULL, *out = NULL, *global = NULL;

    strcpy (channel, IRC_CHANNEL);

    sprintf (in_path, SERVER "/%s/out", channel);
    sprintf (out_path, SERVER "/%s/in", channel);

    if (!(global = fopen (SERVER "/in", "w"))) {
        perror ("fopen " SERVER "/in");
        return 1;
    }

    /* join channel */
    if (!(in = fopen (in_path, "r"))) {
        fprintf (global, "/j %s\n", channel);
        fflush (global);

        /* wait for ii to create the files and such */
        sleep (3);
        in = fopen (in_path, "r");
    }

    out = fopen (out_path, "w");

    if (!in || !out) {
        perror ("fopen " SERVER "/<channel>/[in,out]");
        return 1;
    }

    srand (time (NULL));
    fseek (in, 0, SEEK_END);
    int pos = ftell (in);

    struct context ctx;
    ctx.last_quote = time(NULL) - INTERVAL;
    ctx.print_no = 1;
    ctx.global = global;
    ctx.channel = out;

    while (1) {
        clearerr (in);
        memset (buffer, 0, sizeof buffer);
        if (fgets (buffer, sizeof buffer, in)) {
            sflush (buffer);
            char *cmd = find_message (buffer);
            if (cmd) {
                run_cmd (&ctx, buffer, cmd);
            }
        } else {
            sleep (1);
        }
    }

    fclose (in);
    fclose (out);

    return 0;
}
