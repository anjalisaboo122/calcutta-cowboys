/*
 * logtop.c
 *
 * Usage: ./logtop <logfile> <column>
 *
 * Reproduces, via fork()+pipe()+dup2()+exec() only (no manual counting
 * or sorting logic in this file):
 *
 *   cut -d' ' -f<column> <logfile> | sort | uniq -c | sort -rn | head -5
 *
 * 5 external programs  =>  4 pipes connecting consecutive stages.
 * Stage 0's input is the file itself (passed as an argument to cut,
 * not via stdin). Stage 4's output is left going to our real stdout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#define NSTAGES 5
#define NPIPES  (NSTAGES - 1)

static void close_all_pipes(int pipes[NPIPES][2]) {
    for (int j = 0; j < NPIPES; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <logfile> <column>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *logfile = argv[1];
    const char *column  = argv[2];

    /* Validate column is a positive integer, e.g. reject "abc" or "-1" */
    char *endptr;
    long col_val = strtol(column, &endptr, 10);
    if (*endptr != '\0' || col_val <= 0) {
        fprintf(stderr, "Error: column must be a positive integer, got \"%s\"\n", column);
        return EXIT_FAILURE;
    }

    /* Fail fast with a clear message if the log file isn't readable,
       instead of letting a cryptic error surface from inside cut. */
    if (access(logfile, R_OK) != 0) {
        fprintf(stderr, "Error: cannot read log file \"%s\": %s\n", logfile, strerror(errno));
        return EXIT_FAILURE;
    }

    int pipes[NPIPES][2];
    for (int j = 0; j < NPIPES; j++) {
        if (pipe(pipes[j]) == -1) {
            perror("pipe");
            return EXIT_FAILURE;
        }
    }

    pid_t pids[NSTAGES];

    for (int i = 0; i < NSTAGES; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            /* ---------- Child: stage i of the pipeline ---------- */

            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);   /* read from previous stage */
            if (i < NPIPES)
                dup2(pipes[i][1], STDOUT_FILENO);       /* write to next stage      */

            /* Every pipe fd must be closed in every child, including
               the ones we just dup'd -- dup2 leaves the originals open
               too, and any leftover copy of a write end can make a
               downstream reader hang forever waiting for EOF. */
            close_all_pipes(pipes);

            switch (i) {
                case 0: /* cut -d' ' -f<column> <logfile> */
                    execlp("cut", "cut", "-d", " ", "-f", column, logfile, (char *)NULL);
                    break;
                case 1: /* sort */
                    execlp("sort", "sort", (char *)NULL);
                    break;
                case 2: /* uniq -c */
                    execlp("uniq", "uniq", "-c", (char *)NULL);
                    break;
                case 3: /* sort -rn */
                    execlp("sort", "sort", "-rn", (char *)NULL);
                    break;
                case 4: /* head -5 */
                    execlp("head", "head", "-5", (char *)NULL);
                    break;
            }

            /* Only reached if execlp() itself failed */
            fprintf(stderr, "logtop: exec failed for stage %d: %s\n", i, strerror(errno));
            _exit(EXIT_FAILURE);
        }

        pids[i] = pid; /* ---------- Parent: remember this child's pid, keep forking ---------- */
    }

    /* Parent needs none of the pipe fds itself -- close every last one
       BEFORE waiting, so EOF can actually propagate down the chain. */
    close_all_pipes(pipes);

    int status, exit_code = EXIT_SUCCESS;
    for (int i = 0; i < NSTAGES; i++) {
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            exit_code = EXIT_FAILURE; /* propagate a failure from any stage */
    }

    return exit_code;
}