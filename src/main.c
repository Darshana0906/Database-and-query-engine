/* main.c - interactive shell for GateDB */
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>

static Catalog cat;
static int print_rows = 1;

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

static int run_text(const char *text) {              /* returns 0 on QUIT */
    Stmt st; char err[256];
    if (!sql_parse(text, &st, err, sizeof err)) { printf("ERROR: %s\n", err); return 1; }
    if (st.kind == ST_QUIT) return 0;
    ExecInfo info = { 0 };
    if (!engine_exec(&cat, &st, stdout, print_rows, &info, err, sizeof err)) printf("ERROR: %s\n", err);
    return 1;
}

static int shell_cmd(char *line);

static int run_stream(FILE *in, int interactive) {   /* returns 0 on QUIT */
    char line[4096], buf[16384]; buf[0] = '\0';
    for (;;) {
        if (interactive) { fputs(buf[0] ? "     ...> " : "gatedb> ", stdout); fflush(stdout); }
        if (!fgets(line, sizeof line, in)) break;
        char *l = trim(line);
        if (!buf[0] && (!*l || !strncmp(l, "--", 2))) continue;
        if (!buf[0] && *l == '\\') { if (!shell_cmd(l)) return 0; continue; }
        if (strlen(buf) + strlen(l) + 2 < sizeof buf) { strcat(buf, l); strcat(buf, " "); }
        size_t n = strlen(l);
        if (n && l[n - 1] == ';') { if (!interactive) printf("gatedb> %s\n", buf); if (!run_text(buf)) return 0; buf[0] = '\0'; }
    }
    if (trim(buf)[0] && !run_text(buf)) return 0;
    return 1;
}

static int shell_cmd(char *line) {                   /* returns 0 on quit */
    char a[64] = "", b[256] = "", c[64] = "";
    sscanf(line, "%63s %255s %63s", a, b, c);
    if (!strcmp(a, "\\q")) return 0;
    if (!strcmp(a, "\\rows")) { print_rows = strcasecmp(b, "off") != 0; printf("row printing %s\n", print_rows ? "on" : "off"); }
    else if (!strcmp(a, "\\opt")) {
        int on = strcasecmp(c, "off") != 0;
        if (!strcmp(b, "indexes")) { engine_opts.use_indexes = on; printf("index usage %s\n", on ? "on" : "off (planner forced to full scans / hash joins)"); }
        else if (!strcmp(b, "heap")) { engine_opts.use_topk_heap = on; printf("top-K heap %s\n", on ? "on" : "off (full sort)"); }
        else puts("usage: \\opt indexes|heap on|off");
    }
    else if (!strcmp(a, "\\i")) {
        FILE *f = fopen(b, "r");
        if (!f) printf("ERROR: cannot open '%s'\n", b);
        else { int cont = run_stream(f, 0); fclose(f); if (!cont) return 0; }
    }
    else if (!strcmp(a, "\\h") || !strcmp(a, "\\?")) engine_help(stdout);
    else printf("unknown command %s (try HELP;)\n", a);
    return 1;
}

int main(int argc, char **argv) {
    catalog_init(&cat);
    FILE *in = stdin; const char *script = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "-q")) print_rows = 0;
        else { fprintf(stderr, "usage: %s [-f script.sql] [-q]\n", argv[0]); return 1; }
    }
    if (script && !(in = fopen(script, "r"))) { perror(script); return 1; }
    int interactive = !script && isatty(fileno(stdin));
    if (interactive) puts("GateDB - a tiny query engine in C for the hostel entry system.  Type HELP; for syntax, \\q to quit.");
    run_stream(in, interactive);
    return 0;
}
