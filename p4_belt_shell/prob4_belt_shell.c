/*
 * CS F372 - Assignment 1 - Problem 4: belt_shell
 *
 * A minimal shell for a warehouse conveyor-belt controller.
 *   internal (no fork) : add_item <name>, list_items, quit
 *   external (fork+exec): date, ping <address>   (exactly 4 pings)
 *
 * Ctrl+C never kills the shell - it raises an emergency stop that clears
 * the item queue and returns to the prompt. Only `quit` exits.
 *
 * Compile: gcc -Wall -Wextra -o belt_shell prob4_belt_shell.c
 * Run    : ./belt_shell
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT_LEN 1023              /* longest line we must accept     */
#define MAX_LINE    (MAX_INPUT_LEN + 2) /* + '\n' + '\0'                   */
#define MAX_ITEMS   10                  /* belt queue capacity             */
#define MAX_NAME    64                  /* longest item name we store      */
#define MAX_TOKENS  64                  /* command + arguments             */

/* Set to 1 by the SIGINT handler. The handler does nothing else:
 * printing and clearing the queue happen in main(), in normal code. */
volatile sig_atomic_t emergency_stop = 0;

static void sigint_handler(int signo)
{
    (void)signo;
    emergency_stop = 1;
}

/* Split "line" in place into tokens. tokens[] is NULL-terminated so it can
 * be handed straight to execvp(). Returns the number of tokens found. */
static int split_line(char *line, char *tokens[], int max_tokens)
{
    int count = 0;
    char *tok = strtok(line, " \t\r\n");

    while (tok != NULL && count < max_tokens - 1) {
        tokens[count++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    tokens[count] = NULL;
    return count;
}

/* fork -> child execs the program -> parent waits for it to finish. */
static void run_external(char *const argv[])
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("belt_shell: fork");
        return;
    }

    if (pid == 0) {                       /* ---- child ---- */
        execvp(argv[0], argv);
        /* only reached if exec failed */
        fprintf(stderr, "belt_shell: cannot run %s: %s\n",
                argv[0], strerror(errno));
        _exit(127);
    }

    /* ---- parent ---- */
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {             /* EINTR = a signal woke us up */
            perror("belt_shell: waitpid");
            break;
        }
    }
}

int main(void)
{
    char  queue[MAX_ITEMS][MAX_NAME];
    int   item_count = 0;
    char  line[MAX_LINE];
    char *tokens[MAX_TOKENS];
    int   ntok, i;

    /* Install the SIGINT handler. sa_flags = 0 (no SA_RESTART) so that a
     * Ctrl+C interrupts a blocking read instead of silently resuming it. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("belt_shell: sigaction");
        return 1;
    }

    for (;;) {
        /* ---------- step 0: did a Ctrl+C arrive since last time? ---------- */
        if (emergency_stop) {
            emergency_stop = 0;
            item_count = 0;
            printf("\n[ALERT] Emergency stop triggered, item queue cleared\n");
        }

        /* ---------- step 1: print the prompt ---------- */
        printf("belt-control$ ");
        fflush(stdout);

        /* ---------- step 2: read one line ---------- */
        if (fgets(line, sizeof line, stdin) == NULL) {
            if (emergency_stop) {        /* Ctrl+C interrupted the read */
                clearerr(stdin);         /* (also clears the EOF flag)  */
                continue;                /* top of loop prints the alert */
            }
            if (feof(stdin)) {           /* Ctrl+D / end of piped input */
                printf("\n");
                break;
            }
            if (errno == EINTR) {       /* some other signal woke us up */
                clearerr(stdin);
                continue;               /* never exit on a signal */
            }
            perror("belt_shell: read"); /* a genuine read error */
            break;
        }

        /* Over-long line: consume the rest of it so the leftovers are not
         * mistaken for the next command. */
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            fprintf(stderr, "belt_shell: input too long (max %d characters)\n",
                    MAX_INPUT_LEN);
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            continue;
        }

        /* ---------- step 3: parse it ---------- */
        ntok = split_line(line, tokens, MAX_TOKENS);
        if (ntok == 0)                   /* user just pressed Enter */
            continue;

        /* ---------- step 4: dispatch ---------- */

        /* ----- internal commands: no fork ----- */
        if (strcmp(tokens[0], "quit") == 0) {
            break;
        }
        else if (strcmp(tokens[0], "add_item") == 0) {
            if (ntok < 2)
                fprintf(stderr, "add_item: missing item name\n");
            else if (item_count >= MAX_ITEMS)
                fprintf(stderr, "add_item: queue is full (max %d items)\n",
                        MAX_ITEMS);
            else {
                strncpy(queue[item_count], tokens[1], MAX_NAME - 1);
                queue[item_count][MAX_NAME - 1] = '\0';
                item_count++;
            }
        }
        else if (strcmp(tokens[0], "list_items") == 0) {
            if (item_count == 0)
                printf("Queue is empty\n");
            else
                for (i = 0; i < item_count; i++)
                    printf("%s\n", queue[i]);
        }
        /* ----- external commands: fork + exec + wait ----- */
        else if (strcmp(tokens[0], "date") == 0) {
            char *argv[] = { "date", NULL };
            run_external(argv);
        }
        else if (strcmp(tokens[0], "ping") == 0) {
            if (ntok < 2)
                fprintf(stderr, "ping: missing address\n");
            else {
                char *argv[] = { "ping", "-c", "4", tokens[1], NULL };
                run_external(argv);
            }
        }
        /* ----- anything else ----- */
        else {
            fprintf(stderr, "belt_shell: %s: command not found\n", tokens[0]);
        }
    }

    return 0;
}
