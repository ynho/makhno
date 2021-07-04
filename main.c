#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#ifdef PROD
 #define INTERVAL 45
 #define QUOTES "quotes"
 #define IRC_CHANNEL "##politics"
#else
 #define INTERVAL 5
 #define QUOTES "testquotes"
 #define IRC_CHANNEL "##mako"
#endif
#define SERVER "irc.libera.chat"
#define QUOTE_SIZE 1024
#define NICKNAME_SIZE 64
#define MIN_SEARCH_PATTERN 4
#define MAX_MATCHES 5

struct context {
    time_t last_quote;
    time_t last_search;
    time_t last_wrong_search;
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
    return strstr (a, b) == a;
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

static time_t extract_timestamp (char *str) {
    char number[32] = {0};
    get_timestamp (str, number);
    return (time_t)strtol(number, NULL, 10);
}

static void extract_nickname (char *str, char nickname[NICKNAME_SIZE]) {
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

static void printquote_from_string (FILE *out, int n, char *string) {
    char nickname[NICKNAME_SIZE] = {0};
    char *quote = extract_quote (string);
    extract_nickname (string, nickname);
    fprintf (out, "quote %d by %s: %s\n", n, nickname, quote);
    fflush (out);
}

/* n must be between 1 and $(wc -l quotes), included */
static void printquote (FILE *out, FILE *q, int n) {
    char buffer[QUOTE_SIZE] = {0};
    int count = 0, c;
    while (count < (n - 1) && (c = fgetc (q)) != EOF)
        if (c == '\n') count++;
    if (fgets (buffer, sizeof buffer, q)) {
        sflush(buffer);
        /* time_t ts = extract_timestamp (buffer); */
        printquote_from_string (out, n, buffer);
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
                int r = randrange (1, lines);
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
                printquote (ctx->channel, q, lines);
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

static int match_pattern (char *quote, char *pattern) {
    return strstr (quote, pattern) != NULL;
}

static int match (FILE *q, char *pattern, int matches[MAX_MATCHES], int *num, char *last) {
    int i = 0, line = 1, s;
    char buffer[QUOTE_SIZE] = {0};
    *num = 0;
    while (fgets(buffer, QUOTE_SIZE, q)) {
        if (match_pattern (extract_quote (buffer), pattern)) {
            matches[i] = line;
            s = i;
            strcpy (last, buffer);
            i = (i + 1) % MAX_MATCHES;
            *num = *num + 1;
        }
        memset (buffer, 0, sizeof buffer);
        line++;
    }
    return *num <= MAX_MATCHES ? 0 : i;
}

static void findquote (struct context *ctx, char *pattern) {
    time_t now = time (NULL);
    if (now - ctx->last_search >= INTERVAL) {
        if (strlen (pattern) < MIN_SEARCH_PATTERN) {
            if (now - ctx->last_wrong_search >= INTERVAL) {
                fprintf (ctx->channel, "search pattern must contain at least %d characters\n",
                         MIN_SEARCH_PATTERN);
                fflush (ctx->channel);
                ctx->last_wrong_search = now;
            }
        } else {
            FILE *q = NULL;
            if (q = fopen (QUOTES, "r")) {
                int matches[MAX_MATCHES], num;
                char last[QUOTE_SIZE] = {0};
                for (int i = 0; i < MAX_MATCHES; i++) matches[i] = 0;
                int first = match (q, pattern, matches, &num, last);
                if (num > 0) {
                    if (num > MAX_MATCHES) {
                        fprintf (ctx->channel, "%d matches, last %d:", num, MAX_MATCHES);
                        num = MAX_MATCHES;
                    } else {
                        fprintf (ctx->channel, "%d matches:", num);
                    }
                    for (int i = 0; i < num; i++)
                        fprintf (ctx->channel, " %d", matches[(i + first) % MAX_MATCHES]);
                    fprintf (ctx->channel, "\n");
                    fflush (ctx->channel);
                    printquote_from_string (ctx->channel, matches[(first + num - 1) % MAX_MATCHES], last);
                } else {
                    fprintf (ctx->channel, "pattern not found\n");
                    fflush (ctx->channel);
                }
                fclose (q);
            } else {
                noquotes (ctx);
            }
            ctx->last_search = now;
        }
    }
}

static void run_cmd (struct context *ctx, char *msg, char *cmd) {
    if (strstart (cmd, "!addquote")) {
        addquote (ctx->global, msg, &cmd[strlen("!addquote") + 1]);
    } else if (strstart (cmd, "!randquote")) {
        randquote (ctx, msg);
    } else if (strstart (cmd, "!lastquote")) {
        lastquote (ctx);
    } else if (strstart (cmd, "!findquote")) {
        findquote (ctx, &cmd[strlen("!findquote") + 1]);
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
    ctx.last_quote = 0;
    ctx.last_search = 0;
    ctx.last_wrong_search = 0;
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
