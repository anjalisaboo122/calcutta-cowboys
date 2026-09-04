/*
 * logtop.c
 *
 * Usage: ./logtop <logfile> <column>
 *
 * Builds this pipeline manually using fork()+pipe()+dup2()+exec() only,
 * no counting or sorting logic written by hand in this file:
 *
 *   cut -d' ' -f<column> <logfile> | sort | uniq -c | sort -rn | head -5
 *
 * 5 programs total, so 4 pipes connect them.
 * Stage 0 reads the file directly as an arg to cut, not from stdin.
 * Stage 4's output just goes to our normal stdout, nothing special.
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

    // make sure column is actually a positive number, catch stuff like "abc" or "-1" early
    char *endptr;
    long col_val = strtol(column, &endptr, 10);
    if (*endptr != '\0' || col_val <= 0) {
        fprintf(stderr, "Error: column must be a positive integer, got \"%s\"\n", column);
        return EXIT_FAILURE;
    }

    // check the file is readable now instead of letting cut fail later
    // with some confusing error buried in the pipeline
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
            // ---------- Child: this is stage i of the pipeline ----------

            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);   // read from the previous stage's pipe
            if (i < NPIPES)
                dup2(pipes[i][1], STDOUT_FILENO);       // write into the next stage's pipe

            // gotta close every pipe fd here, even the ones we just dup'd.
            // dup2 doesn't close the original, so if we leave any write end
            // open by accident, whoever's reading downstream just hangs
            // forever waiting for an EOF that never comes
            close_all_pipes(pipes);

            switch (i) {
                case 0: // cut -d' ' -f<column> <logfile>
                    execlp("cut", "cut", "-d", " ", "-f", column, logfile, (char *)NULL);
                    break;
                case 1: // sort
                    execlp("sort", "sort", (char *)NULL);
                    break;
                case 2: // uniq -c
                    execlp("uniq", "uniq", "-c", (char *)NULL);
                    break;
                case 3: // sort -rn
                    execlp("sort", "sort", "-rn", (char *)NULL);
                    break;
                case 4: // head -5
                    execlp("head", "head", "-5", (char *)NULL);
                    break;
            }

            // only get here if execlp actually failed to run
            fprintf(stderr, "logtop: exec failed for stage %d: %s\n", i, strerror(errno));
            _exit(EXIT_FAILURE);
        }

        pids[i] = pid; // ---------- Parent: just save the pid and keep forking the next stage ----------
    }

    // parent doesn't need any pipe fds at all, close everything
    // BEFORE waiting, otherwise EOF can't travel down the chain properly
    close_all_pipes(pipes);

    int status, exit_code = EXIT_SUCCESS;
    for (int i = 0; i < NSTAGES; i++) {
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            exit_code = EXIT_FAILURE; // if any single stage failed, whole thing counts as a fail
    }

    return exit_code;
}