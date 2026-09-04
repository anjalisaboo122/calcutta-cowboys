/*
 * CS F372: Assignment 1 Problem 4: belt_shell
 *
 * A minimal shell for a warehouse conveyor-belt controller.
 *   internal (no fork) : add_item <name>, list_items, quit
 *   external (fork+exec): date, ping <address>   (exactly 4 pings)
 *
 * Ctrl+C never kills the shell: it raises an emergency stop that clears
 * the item queue and returns to the prompt. Only 'quit' exits.
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

#define MAX_INPUT_LEN 1023              // longest line we're willing to accept
#define MAX_LINE    (MAX_INPUT_LEN + 2) // +1 for '\n', +1 for '\0'
#define MAX_ITEMS   10                  // how many items the belt queue can hold
#define MAX_NAME    64                  // longest item name we'll store
#define MAX_TOKENS  64                  // command plus its args

// gets set to 1 inside the SIGINT handler. handler does nothing else,
// all the actual printing and queue clearing happens in main() as normal code
volatile sig_atomic_t emergency_stop = 0;

static void sigint_handler(int signo)
{
    (void)signo;
    emergency_stop = 1;
}

// splits "line" into tokens in place. tokens[] ends with NULL so it can
// be passed directly into execvp(). returns how many tokens it found
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

// fork, child execs the program, parent just waits for it
static void run_external(char *const argv[])
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("belt_shell: fork");
        return;
    }

    if (pid == 0) {                       // ---- child ----
        execvp(argv[0], argv);
        // only reach here if exec actually failed
        fprintf(stderr, "belt_shell: cannot run %s: %s\n",
                argv[0], strerror(errno));
        _exit(127);
    }

    // ---- parent ----
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {             // EINTR just means a signal woke us up, not a real error
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

    // set up the SIGINT handler. sa_flags = 0 (no SA_RESTART) on purpose,
    // so ctrl+C actually interrupts a blocking read instead of the read
    // just quietly resuming like nothing happened
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
        // ---------- step 0: did ctrl+C happen since the last loop? ----------
        if (emergency_stop) {
            emergency_stop = 0;
            item_count = 0;
            printf("\n[ALERT] Emergency stop triggered, item queue cleared\n");
        }

        // ---------- step 1: print the prompt ----------
        printf("belt-control$ ");
        fflush(stdout);

        // ---------- step 2: read one line ----------
        if (fgets(line, sizeof line, stdin) == NULL) {
            if (emergency_stop) {        // ctrl+C is what interrupted the read
                clearerr(stdin);         // this also clears the EOF flag
                continue;                // loop back around, top will print the alert
            }
            if (feof(stdin)) {           // ctrl+D or piped input just ended
                printf("\n");
                break;
            }
            if (errno == EINTR) {       // some other signal woke us up
                clearerr(stdin);
                continue;               // never exit just because of a signal
            }
            perror("belt_shell: read"); // this is an actual read error
            break;
        }

        // line was too long: drain the rest of it so leftover chars don't
        // get mistaken for the start of the next command
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            fprintf(stderr, "belt_shell: input too long (max %d characters)\n",
                    MAX_INPUT_LEN);
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            continue;
        }

        // ---------- step 3: parse it ----------
        ntok = split_line(line, tokens, MAX_TOKENS);
        if (ntok == 0)                   // user just hit enter, nothing to do
            continue;

        // ---------- step 4: figure out what to do with it ----------

        // ----- internal commands, no fork needed ----- 
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
        // ----- external commands, actually fork + exec + wait -----
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
        // ----- anything else, not a real command -----
        else {
            fprintf(stderr, "belt_shell: %s: command not found\n", tokens[0]);
        }
    }

    return 0;
}
