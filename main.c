#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#define SELF "makhno"

#ifdef PROD
 #define INTERVAL 45
 #define NAMES_INTERVAL 2
 #define QUOTES "quotes"
 #define CHANNEL "##politics"
 #define QUERY_NAMES_TIMEOUT 10
 #define QUERY_WHOIS_TIMEOUT 5
 #define MIN_VOTES_TO_DELETE 3
 #define VOTE_DURATION 120
#else
 #define INTERVAL 1
 #define NAMES_INTERVAL 1
 #define QUOTES "testquotes"
 #define CHANNEL "##mako"
 #define QUERY_NAMES_TIMEOUT 3
 #define QUERY_WHOIS_TIMEOUT 7
 #define MIN_VOTES_TO_DELETE 1
 #define VOTE_DURATION 7
#endif
#define SERVER "irc.libera.chat"
#define QUOTE_SIZE 1024
#define NICKNAME_SIZE 64
#define MIN_SEARCH_PATTERN 3
#define MAX_MATCHES 5
#define NUM_VOICED 100
#define MAX_VOTES 1000

struct context {
    time_t last_quote;
    time_t last_search;
    time_t last_wrong_search;
    time_t last_help;
    time_t last_names;
    time_t last_when;
    int print_no;
    FILE *global_in;
    FILE *global_out;
    FILE *channel;
    char voiced[100][NICKNAME_SIZE];
    int n_voiced;

    time_t vote_ts;
    int vote_quote;
    char votes[2][MAX_VOTES][NICKNAME_SIZE];
    size_t n_votes[2];
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

static int get_timestamp (char *msg, char *number) {
    int i = 0;
    while (*msg <= '9' && *msg >= '0')
        number[i++] = *msg++;
    return i;
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

static int strstart (char *a, char *b) {
    return strstr (a, b) == a;
}

static void read_names (struct context *ctx, char *string) {
    int i = 0, incr = 0;
    memset (ctx->voiced[ctx->n_voiced], 0, NICKNAME_SIZE);
    while (*string) {
        if (*string == '+' || *string == '@') {
            incr = 1;
            i = 0;
            memset (ctx->voiced[ctx->n_voiced], 0, NICKNAME_SIZE);
        } else if (*string == ' ') {
            ctx->n_voiced += incr;
            incr = 0;
            i = 0;
        } else if (incr == 1) {
            ctx->voiced[ctx->n_voiced][i] = *string;
            i++;
        }
        string++;
    }
    ctx->n_voiced += incr;
}

static void names (struct context *ctx) {
    char buffer[1024] = {0};
    time_t ts = time(NULL);
    ctx->n_voiced = 0;
    fseek (ctx->global_in, 0, SEEK_END);
    fprintf (ctx->global_out, "/NAMES "CHANNEL"\n");
    fflush (ctx->global_out);
    while (time(NULL) - ts < QUERY_NAMES_TIMEOUT) {
        memset (buffer, 0, sizeof buffer);
        if (fgets (buffer, sizeof buffer, ctx->global_in)) {
            sflush (buffer);
            if (strstart (&buffer[10], " = "CHANNEL" ")) {
                char *ptr = &buffer[10 + strlen (" = "CHANNEL" ")];
                read_names (ctx, ptr);
            } else if (strstart (&buffer[10], " "CHANNEL" End of /NAMES"))
                break;
        } else
            clearerr (ctx->global_in);
    }
    if (ctx->n_voiced <= 0)
        printf ("WARNING: no voiced people were added... suspicious\n");
}

static int count_lines (FILE *q) {
    int count = 0, c;
    while ((c = fgetc (q)) != EOF)
        if (c == '\n') count++;
    return count;
}

static int authorized (struct context *ctx, char *nickname) {
    time_t now = time (NULL);
    if (now - ctx->last_names > NAMES_INTERVAL) {
        names (ctx);
        ctx->last_names = now;
    }
    for (int i = 0; i < ctx->n_voiced; i++) {
        if (!strcmp (ctx->voiced[i], nickname))
            return 1;
    }
    return 0;
}

static long extract_number_ (char *str, int *success) {
    char number[32] = {0};
    if (get_timestamp (str, number) > 0) {
        if (success)
            *success = 1;
        return strtol (number, NULL, 10);
    } else {
        if (success)
            *success = 0;
        return 0;
    }
}

static long extract_number (char *str) {
    return extract_number_ (str, NULL);
}

static int getlinen (FILE *q, int n, char buffer[QUOTE_SIZE]) {
    int count = 0, c;
    while (count < (n - 1) && (c = fgetc (q)) != EOF)
        if (c == '\n') count++;
    if (fgets (buffer, QUOTE_SIZE, q)) {
        sflush (buffer);
        return 1;
    } else
        return 0;
}

static void addquote (struct context *ctx, char *msg, char *quote) {
    FILE *out = ctx->global_out;
    if (strlen (quote) > 0) {
        char nickname[NICKNAME_SIZE] = {0};
        char timestamp[32] = {0};
        copy_nickname (msg, nickname);
#ifdef PROD
        if (authorized (ctx, nickname)) {
#endif
            get_timestamp (msg, timestamp);
            FILE *q = fopen (QUOTES, "r+");
            long nb = 1;
            char buffer[QUOTE_SIZE] = {0};
            int n_lines = count_lines (q);
            rewind (q);
            if (getlinen (q, n_lines, buffer))
                nb = extract_number (buffer) + 1;
            fprintf (q, "%d;%s;%s;%s\n", nb, timestamp, nickname, quote);
            fclose (q);
            fprintf (out, "/PRIVMSG %s :added quote %d: %s\n", nickname, nb, quote);
            fflush (out);
#ifdef PROD
        } else {
            fprintf (out, "/PRIVMSG %s :only voiced users may add quotes\n", nickname);
            fflush (out);
        }
#endif
    }
}

static int randrange (int a, int b) {
    float r = (float)rand () / RAND_MAX;
    r *= (b - a);
    r += a;
    return r + 0.5;
}

static char* goto_field (char *str, int field) {
    for (int i = 0; i < field; i++) {
        while (*str != ';' && *str != 0) str++;
        if (*str != 0)
            str++;
    }
    return str;
}

static time_t extract_timestamp (char *str) {
    str = goto_field (str, 1);
    return (time_t)extract_number (str);
}


static char* extract_nickname (char *str, char nickname[NICKNAME_SIZE]) {
    int i = 0;
    str = goto_field (str, 2);
    while (*str != ';')
        nickname[i++] = *str++;
    return nickname;
}

static char* extract_quote (char *str) {
    return goto_field (str, 3);
}

static long fetchquote (FILE *q, int n, char buffer[QUOTE_SIZE]) {
    long p = ftell (q);
    while (fgets (buffer, QUOTE_SIZE, q)) {
        long nb = extract_number (buffer);
        if (n == nb) {
            sflush (buffer);
            return p;
        }
        p = ftell (q);
    }
    return -1;
}

static void printquote_from_string (FILE *out, int n, char *string) {
    char nickname[NICKNAME_SIZE] = {0};
    char *quote = extract_quote (string);
    extract_nickname (string, nickname);
    fprintf (out, "quote %d by %s: %s\n", n, nickname, quote);
    fflush (out);
}

/* n must be between 1 and $(wc -l quotes), included */
static int printquotel (FILE *out, FILE *q, int n) {
    char buffer[QUOTE_SIZE] = {0};
    if (getlinen (q, n, buffer)) {
        long nb = extract_number (buffer);
        printquote_from_string (out, nb, buffer);
        return 0;
    } else {
        return 1;
    }
}

/* n must be an existing quote ID */
static int printquoten (FILE *out, FILE *q, int n) {
    char buffer[QUOTE_SIZE] = {0};
    if (fetchquote (q, n, buffer) >= 0) {
        printquote_from_string (out, n, buffer);
        return 0;
    } else {
        return 1;
    }
}

static void printts (FILE *out, int n, time_t ts) {
    char buf[64] = {0};
    struct tm t;
    gmtime_r (&ts, &t);
    strftime (buf, sizeof buf, "%F %T", &t);
    fprintf (out, "%s GMT\n", buf);
    fflush (out);
}

static int printquotets (FILE *out, FILE *q, int n) {
    char buffer[QUOTE_SIZE] = {0};
    if (fetchquote (q, n, buffer) >= 0) {
        time_t ts = extract_timestamp (buffer);
        printts (out, n, ts);
        return 0;
    } else {
        return 1;
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
            rewind (q);
            if (lines > 0) {
                int r = randrange (1, lines);
                printquotel (ctx->channel, q, r);
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
            rewind (q);
            if (lines > 0) {
                printquotel (ctx->channel, q, lines);
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

static char* tolowers (char *s) {
    for (int i = 0; s[i]; i++)
        s[i] = tolower (s[i]);
    return s;
}

static int match (FILE *q, char *pattern, int matches[MAX_MATCHES], int *num, char *last, int author) {
    int i = 0, s;
    char buffer[QUOTE_SIZE] = {0};
    char decap[QUOTE_SIZE] = {0};
    *num = 0;
    while (fgets (buffer, QUOTE_SIZE, q)) {
        char nickname[NICKNAME_SIZE] = {0};
        char *extract = author ? extract_nickname (buffer, nickname) : extract_quote (buffer);
        int number = extract_number (buffer);
        strcpy (decap, extract);
        if (match_pattern (tolowers (decap), pattern)) {
            matches[i] = number;
            s = i;
            strcpy (last, buffer);
            i = (i + 1) % MAX_MATCHES;
            *num = *num + 1;
        }
        memset (buffer, 0, sizeof buffer);
    }
    return *num <= MAX_MATCHES ? 0 : i;
}

static void findquote (struct context *ctx, char *pattern, int author) {
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
                tolowers (pattern);
                int first = match (q, pattern, matches, &num, last, author);
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

static void quote (struct context *ctx, char *arg) {
    time_t now = time (NULL);
    if (now - ctx->last_quote >= INTERVAL) {
        FILE *q = NULL;
        long n = strtol (arg, NULL, 10);
        switch (n) {
        case LONG_MIN:
        case LONG_MAX:
        case 0:
            return;
        default:
            if (q = fopen (QUOTES, "r")) {
                printquoten (ctx->channel, q, n);
                fclose (q);
            } else
                noquotes (ctx);
            ctx->last_quote = now;
        }
    }
}

static void when (struct context *ctx, char *arg) {
    time_t now = time (NULL);
    if (now - ctx->last_when >= INTERVAL) {
        FILE *q = NULL;
        long n = strtol (arg, NULL, 10);
        switch (n) {
        case LONG_MIN:
        case LONG_MAX:
        case 0:
            return;
        default:
            if (q = fopen (QUOTES, "r")) {
                printquotets (ctx->channel, q, n);
                fclose (q);
            } else
                noquotes (ctx);
            ctx->last_when = now;
        }
    }
}

static int running_vote (struct context *ctx) {
    return time (NULL) - ctx->vote_ts < VOTE_DURATION;
}

static int logged (struct context *ctx, char nickname[NICKNAME_SIZE], char id[NICKNAME_SIZE]) {
    char buffer[QUOTE_SIZE] = {0};
    time_t ts = time (NULL);
    fseek (ctx->global_in, 0, SEEK_END);
    fprintf (ctx->global_out, "/WHOIS %s\n", nickname);
    fflush (ctx->global_out);
    while (time (NULL) - ts < QUERY_WHOIS_TIMEOUT) {
        memset (buffer, 0, sizeof buffer);
        if (fgets (buffer, sizeof buffer, ctx->global_in)) {
            sflush (buffer);
            char *pos = strstr (&buffer[10], "is logged in as");
            if (pos) {
                size_t skip = 12 + strlen (nickname);
                int i = 0;
                while (buffer[skip + i] != ' ') {
                    id[i] = buffer[skip + i];
                    i++;
                }
                id[i] = 0;
                return 1;
            }
        } else
            clearerr (ctx->global_in);
    }
    return 0;
}

static void votedel (struct context *ctx, char *msg, char *quote) {
    FILE *out = ctx->global_out;
    if (strlen (quote) > 0) {
        char nickname[NICKNAME_SIZE] = {0};
        char id[NICKNAME_SIZE] = {0};
        copy_nickname (msg, nickname);
#ifdef PROD
        if (!authorized (ctx, nickname)) {
            fprintf (out, "/PRIVMSG %s :must be voiced to start votes\n", nickname);
            fflush (out);
            return;
        }
#endif
        if (!logged (ctx, nickname, id)) {
            fprintf (out, "/PRIVMSG %s :you must be logged with nickserv to start a vote\n",
                     nickname);
            fflush (out);
            return;
        }
        if (running_vote (ctx)) {
            fprintf (out, "/PRIVMSG %s :a vote is currently in progress, retry in %d seconds\n",
                     nickname, ctx->vote_ts + VOTE_DURATION - time (NULL));
            fflush (out);
            return;
        }
        int success;
        int n = extract_number_ (quote, &success);
        char buffer[QUOTE_SIZE] = {0};
        if (!success) {
            fprintf (out, "/PRIVMSG %s :does that look like a number to you\n",
                     nickname, n);
            fflush (out);
            return;
        }
        FILE *q = fopen (QUOTES, "r");
        if (!q) {
            noquotes (ctx);
            return;             /* :/ */
        }
        if (fetchquote (q, n, buffer) < 0) {
            fprintf (out, "/PRIVMSG %s :quote %d does not exist\n",
                     nickname, n);
            fflush (out);
            return;
        }
        ctx->vote_ts = time (NULL);
        ctx->vote_quote = n;
        ctx->n_votes[0] = ctx->n_votes[1] = 0;
        fprintf (ctx->channel, "%s has called a vote to delete quote %d. Type !yes or !no "
                 "to cast your vote (booth in PM). The quote will be "
                 "deleted if it receives a majority, and at least %d total yes votes. Vote"
                 " open for %d seconds.\n",
                 nickname, n, MIN_VOTES_TO_DELETE, VOTE_DURATION);
        fflush (ctx->channel);
    }
}

static int voter_exists (struct context *ctx, char *id) {
    for (int y = 0; y < 2; y++) {
        for (int i = 0; i < ctx->n_votes[y]; i++) {
            if (!strcmp (ctx->votes[y][i], id))
                return 1;
        }
    }
    return 0;
}

static void cast_vote (struct context *ctx, char nickname[NICKNAME_SIZE], int which) {
    char id[NICKNAME_SIZE] = {0};
    FILE *out = ctx->global_out;
#if 0
#ifdef PROD
    if (!authorized (ctx, nickname)) {
        fprintf (out, "/PRIVMSG %s :only voiced users may vote\n", nickname);
        fflush (out);
        return;
    }
#endif
#endif
    if (!logged (ctx, nickname, id)) {
        fprintf (out, "/PRIVMSG %s :you must be logged with nickserv to vote\n",
                 nickname);
        fflush (out);
        return;
    }
    if (running_vote (ctx)) {
        if (voter_exists (ctx, id)) {
            fprintf (out, "/PRIVMSG %s :you already voted, but nice try.\n", nickname);
            fflush (out);
        } else {
            strcpy (ctx->votes[which][ctx->n_votes[which]], id);
            ctx->n_votes[which]++;
            fprintf (out, "/PRIVMSG %s :you voted %s\n", nickname, which ? "yes" : "no");
            fflush (out);
        }
    }
}

static void delete_quote (struct context *ctx, int n) {
    FILE *q = NULL;
    char buffer[QUOTE_SIZE] = {0};
    if (q = fopen (QUOTES, "r")) {
        long pos = fetchquote (q, n, buffer);
        if (pos < 0) {
            fprintf (stderr, "ERROR: couldnt find the quote we are supposed to delete... "
                     "that shouldnt happen\n");
            return;
        }
        fseek (q, 0, SEEK_END);
        long size = ftell (q);
        char *tmp = malloc (size + 1);
        long i;
        rewind (q);
        for (i = 0; i < pos; i++)
            tmp[i] = fgetc (q);
        while (fgetc (q) != '\n');
        int c;
        while ((c = fgetc (q)) != EOF)
            tmp[i++] = c;
        long written = i;
        tmp[i] = 0;
        fclose (q);
        q = fopen (QUOTES, "w");
        fputs (tmp, q);
        fclose (q);
    }
}

static void end_vote (struct context *ctx) {
    if (ctx->vote_quote) {
        if (!running_vote (ctx)) {
            if (ctx->n_votes[1] >= MIN_VOTES_TO_DELETE &&
                ctx->n_votes[1] > ctx->n_votes[0]) {
                delete_quote (ctx, ctx->vote_quote);
                fprintf (ctx->channel, "vote passed with %d for, %d against. quote %d deleted\n",
                         ctx->n_votes[1], ctx->n_votes[0], ctx->vote_quote);
                fflush (ctx->channel);
            } else {
                fprintf (ctx->channel, "vote failed with %d for, %d against. quote %d remains\n",
                         ctx->n_votes[1], ctx->n_votes[0], ctx->vote_quote);
                fflush (ctx->channel);
            }
            ctx->vote_quote = 0;
            ctx->n_votes[0] = ctx->n_votes[1] = 0;
        }
    }
}

static void help (struct context *ctx) {
    time_t now = time (NULL);
    if (now - ctx->last_help >= INTERVAL) {
        fprintf (ctx->channel, "commands: !addquote <quote>, !randquote, "
                 "!lastquote, !findquote <string>, !quote <number>, !when <number>, "
                 "!findauthor <nick>, !votedel <quote>\n");
        fflush (ctx->channel);
        ctx->last_help = now;
    }
}

static void run_priv_cmd (struct context *ctx, char nickname[NICKNAME_SIZE], char *cmd) {
    if (strstart (cmd, "!yes") ||
               strstart (cmd, "!yep")) {
        cast_vote (ctx, nickname, 1);
    } else if (strstart (cmd, "!no") ||
               strstart (cmd, "!nop") ||
               strstart (cmd, "!nope")) {
        cast_vote (ctx, nickname, 0);
    }
}

static void run_cmd (struct context *ctx, char *msg, char *cmd) {
    char nickname[NICKNAME_SIZE] = {0};
    copy_nickname (msg, nickname);
    if (strstart (cmd, "!addquote")) {
        addquote (ctx, msg, &cmd[strlen("!addquote") + 1]);
    } else if (!strcmp (cmd, "!randquote")) {
        randquote (ctx, msg);
    } else if (!strcmp (cmd, "!lastquote")) {
        lastquote (ctx);
    } else if (strstart (cmd, "!findquote")) {
        findquote (ctx, &cmd[strlen("!findquote") + 1], 0);
    } else if (strstart (cmd, "!findauthor")) {
        findquote (ctx, &cmd[strlen("!findauthor") + 1], 1);
    } else if (strstart (cmd, "!quote")) {
        quote (ctx, &cmd[strlen("!quote") + 1]);
    } else if (!strcmp (cmd, "!help")) {
        help (ctx);
    } else if (strstart (cmd, "!when")) {
        when (ctx, &cmd[strlen("!when") + 1]);
    } else if (strstart (cmd, "!votedel")) {
        votedel (ctx, msg, &cmd[strlen("!votedel") + 1]);
    } else if (!strcmp (cmd, "!yes") ||
               !strcmp (cmd, "!yep")) {
        cast_vote (ctx, nickname, 1);
    } else if (!strcmp (cmd, "!no") ||
               !strcmp (cmd, "!nop") ||
               !strcmp (cmd, "!nope")) {
        cast_vote (ctx, nickname, 0);
    /* } else if (strstart (cmd, "!delquote")) { */
    /*     int n = extract_number (&cmd[strlen("!delquote") + 1]); */
    /*     delete_quote (ctx, n); */
    }
}

static void handle_pm (struct context *ctx, char *msg) {
    char nickname[QUOTE_SIZE] = {0};
    int n;
    char test;
    if (2 == sscanf (msg, "%*s :%s PRIVMSG "SELF" :%n%c", nickname, &n, &test)) {
        int i;
        for (i = 0; nickname[i] != '!' && nickname[i]; i++);
        nickname[i] = 0;
        run_priv_cmd (ctx, nickname, &msg[n]);
    }
}


int main (int argc, char **argv) {
    char buffer[QUOTE_SIZE];
    char channel[256], in_path[256], out_path[256];
    FILE *in = NULL, *out = NULL, *global_in = NULL, *global_out = NULL, *glob = NULL;

    strcpy (channel, CHANNEL);

    sprintf (in_path, SERVER "/%s/out", channel);
    sprintf (out_path, SERVER "/%s/in", channel);

    if (!(global_out = fopen (SERVER "/in", "w"))) {
        perror ("fopen " SERVER "/in");
        return 1;
    }
    if (!(global_in = fopen (SERVER "/out", "r"))) {
        perror ("fopen " SERVER "/out");
        return 1;
    }
    if (!(glob = fopen (SERVER "/glob", "r"))) {
        perror ("fopen " SERVER "/glob");
        return 1;
    }

    /* join channel */
    fprintf (global_out, "/j %s\n", channel);
    fflush (global_out);

    /* wait for ii to create the files and such */
    sleep (3);

    in = fopen (in_path, "r");
    out = fopen (out_path, "w");

    if (!in || !out) {
        perror ("fopen " SERVER "/<channel>/[in,out]");
        return 1;
    }

    srand (time (NULL));
    fseek (glob, 0, SEEK_END);
    fseek (in, 0, SEEK_END);
    int pos = ftell (in);

    struct context *ctx = malloc (sizeof *ctx);
    ctx->last_quote = 0;
    ctx->last_search = 0;
    ctx->last_wrong_search = 0;
    ctx->last_help = 0;
    ctx->last_names = 0;
    ctx->last_when = 0;
    ctx->print_no = 1;
    ctx->global_in = global_in;
    ctx->global_out = global_out;
    ctx->channel = out;
    ctx->n_voiced = 0;
    ctx->vote_ts = 0;
    ctx->vote_quote = 0;
    for (int i = 0; i < MAX_VOTES; i++) {
        memset (ctx->votes[0][i], 0, NICKNAME_SIZE);
        memset (ctx->votes[1][i], 0, NICKNAME_SIZE);
    }
    ctx->n_votes[0] = 0;
    ctx->n_votes[0] = 0;


    while (1) {
        clearerr (in);
        clearerr (glob);
        memset (buffer, 0, sizeof buffer);
        if (fgets (buffer, sizeof buffer, in)) {
            sflush (buffer);
            char *cmd = find_message (buffer);
            if (cmd) {
                run_cmd (ctx, buffer, cmd);
            }
        } else if (fgets (buffer, sizeof buffer, glob)) {
            sflush (buffer);
            handle_pm (ctx, buffer);
        } else {
            sleep (1);
        }
        end_vote (ctx);
    }

    fclose (in);
    fclose (out);
    fclose (glob);
    free (ctx);

    return 0;
}
