/*
 * CS F372 - Assignment 1 - Problem 2: Weighted Resource Monitor
 *
 * Parent  : handles user interaction (prompts for a PID every r cycles)
 * Child   : prints top-k processes by usage_score every n seconds,
 *           and kills a chosen process on parent's instruction.
 *
 * Compile: gcc -Wall -o resource_monitor resource_monitor.c
 * Run    : ./resource_monitor
 *          (it will prompt you for n, k, r)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>

/* message struct shared over the SysV queue.
 * mtype 1 = child -> parent  ("my turn, ask the user")
 * mtype 2 = parent -> child  ("here is the pid/command the user typed")
 */
struct msgbuf {
    long mtype;
    long mval;
};

/* only used so the SIGINT handler (which the OS calls with a fixed
 * signature) can clean up the queue + child. Nothing else touches these. */
static int      g_msqid   = -1;
static pid_t    g_childpid = -1;

static void sigint_handler(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n[SIGINT] Shutting down...\n", 28);
    if (g_childpid > 0) kill(g_childpid, SIGTERM);
    if (g_msqid  >= 0) msgctl(g_msqid, IPC_RMID, NULL);
    _exit(0);
}

/* --- CHILD SIDE ------------------------------------------------------- */

/* ps aux -> awk (compute score) -> sort (desc by score) -> head (top k)
 * built entirely with pipe()/fork()/dup2()/execlp() - no system()/popen(). */
static void print_top_k(int k) {
    printf("PID      COMMAND              CPU%%    MEM%%    SCORE\n");

    int pipe1[2], pipe2[2], pipe3[2]; /* ps->awk, awk->sort, sort->head */
    if (pipe(pipe1) < 0 || pipe(pipe2) < 0 || pipe(pipe3) < 0) {
        perror("pipe");
        return;
    }

    char head_n[16];
    snprintf(head_n, sizeof(head_n), "%d", k);

    const char *awk_prog =
        "NR>1{printf \"%-8s %-20s %-7s %-7s %.2f\\n\", "
        "$2,$11,$3,$4,(3*$3+2*$4)}";

    /* Stage 1: ps aux  (writes into pipe1) */
    pid_t p1 = fork();
    if (p1 == 0) {
        dup2(pipe1[1], STDOUT_FILENO);
        close(pipe1[0]); close(pipe1[1]);
        close(pipe2[0]); close(pipe2[1]);
        close(pipe3[0]); close(pipe3[1]);
        execlp("ps", "ps", "aux", NULL);
        perror("execlp ps"); _exit(1);
    }

    /* Stage 2: awk  (reads pipe1, writes pipe2) */
    pid_t p2 = fork();
    if (p2 == 0) {
        dup2(pipe1[0], STDIN_FILENO);
        dup2(pipe2[1], STDOUT_FILENO);
        close(pipe1[0]); close(pipe1[1]);
        close(pipe2[0]); close(pipe2[1]);
        close(pipe3[0]); close(pipe3[1]);
        execlp("awk", "awk", awk_prog, NULL);
        perror("execlp awk"); _exit(1);
    }

    /* Stage 3: sort  (reads pipe2, writes pipe3) */
    pid_t p3 = fork();
    if (p3 == 0) {
        dup2(pipe2[0], STDIN_FILENO);
        dup2(pipe3[1], STDOUT_FILENO);
        close(pipe1[0]); close(pipe1[1]);
        close(pipe2[0]); close(pipe2[1]);
        close(pipe3[0]); close(pipe3[1]);
        execlp("sort", "sort", "-k5,5", "-rn", NULL);
        perror("execlp sort"); _exit(1);
    }

    /* Stage 4: head -n k  (reads pipe3, writes to our real stdout) */
    pid_t p4 = fork();
    if (p4 == 0) {
        dup2(pipe3[0], STDIN_FILENO);
        close(pipe1[0]); close(pipe1[1]);
        close(pipe2[0]); close(pipe2[1]);
        close(pipe3[0]); close(pipe3[1]);
        execlp("head", "head", "-n", head_n, NULL);
        perror("execlp head"); _exit(1);
    }

    /* we (the monitor's child) don't need any pipe ends - close them all,
     * then wait for the whole pipeline to finish before moving on */
    close(pipe1[0]); close(pipe1[1]);
    close(pipe2[0]); close(pipe2[1]);
    close(pipe3[0]); close(pipe3[1]);

    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
    waitpid(p3, NULL, 0);
    waitpid(p4, NULL, 0);
}

/* runs `ps -p <pid> -o comm=,user=,pcpu=,pmem=` via raw pipe/fork/exec
 * (no popen), reads its one line of output back through the pipe. */
static void handle_pid_action(long pid_in) {
    pid_t pid = (pid_t)pid_in;
    char pidbuf[16];
    snprintf(pidbuf, sizeof(pidbuf), "%d", pid);

    int fd[2];
    if (pipe(fd) < 0) {
        perror("pipe");
        return;
    }

    pid_t worker = fork();
    if (worker == 0) {
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        execlp("ps", "ps", "-p", pidbuf, "-o", "comm=,user=,pcpu=,pmem=", NULL);
        perror("execlp ps -p"); _exit(1);
    }

    close(fd[1]); /* parent (child_loop's process) only reads */

    char line[256] = {0};
    ssize_t total = 0, got;
    while (total < (ssize_t)sizeof(line) - 1 &&
           (got = read(fd[0], line + total, sizeof(line) - 1 - total)) > 0) {
        total += got;
    }
    close(fd[0]);
    waitpid(worker, NULL, 0);

    char comm[128] = "?", user[64] = "?";
    double cpu = 0, mem = 0;
    sscanf(line, "%127s %63s %lf %lf", comm, user, &cpu, &mem);

    double score = 3 * cpu + 2 * mem;
    printf("--> PID %d | comm=%s | owner=%s | CPU%%=%.2f | MEM%%=%.2f | score=%.2f\n",
           pid, comm, user, cpu, mem, score);

    if (kill(pid, SIGKILL) == 0)
        printf("--> Process %d killed.\n", pid);
    else
        perror("--> kill failed");
}

static void child_loop(int n, int k, int r, int msqid) {
    struct msgbuf msg;
    int iter = 0;

    while (1) {
        print_top_k(k);
        sleep(n);
        iter++;

        if (iter == r) {
            iter = 0;

            /* tell parent it's their turn */
            msg.mtype = 1;
            msg.mval  = 1;
            msgsnd(msqid, &msg, sizeof(msg.mval), 0);

            /* block until parent responds */
            msgrcv(msqid, &msg, sizeof(msg.mval), 2, 0);
            long pid_in = msg.mval;

            if (pid_in == -2) {
                printf("--> Quit signal received. Removing message queue.\n");
                msgctl(msqid, IPC_RMID, NULL);
                exit(0);
            } else if (pid_in == -1) {
                printf("--> Skip signal received. Resuming monitoring.\n");
            } else {
                handle_pid_action(pid_in);
            }
        }
    }
}

/* --- PARENT SIDE -------------------------------------------------------*/

static void parent_loop(int msqid, pid_t childpid) {
    struct msgbuf msg;

    while (1) {
        /* block until child says "your turn" */
        msgrcv(msqid, &msg, sizeof(msg.mval), 1, 0);

        long pid_in;
        printf("\nEnter PID to act on (-1 = skip, -2 = quit): ");
        fflush(stdout);
        if (scanf("%ld", &pid_in) != 1) {
            /* bad input, treat as skip */
            int c; while ((c = getchar()) != '\n' && c != EOF) {}
            pid_in = -1;
        }

        msg.mtype = 2;
        msg.mval  = pid_in;
        msgsnd(msqid, &msg, sizeof(msg.mval), 0);

        if (pid_in == -2) {
            waitpid(childpid, NULL, 0);
            printf("Parent exiting.\n");
            break;
        }
    }
}

int main(void) {
    int n, k, r;

    printf("Enter n (seconds between prints), k (processes per print), "
           "r (iterations before prompt):\n");
    if (scanf("%d %d %d", &n, &k, &r) != 3 || n <= 0 || k <= 0 || r <= 0) {
        fprintf(stderr, "n, k, r must all be positive integers.\n");
        exit(1);
    }

    /* message queue MUST exist before fork() so both processes share it */
    key_t key = ftok(".", 'R');
    if (key == -1) { perror("ftok"); exit(1); }

    int msqid = msgget(key, IPC_CREAT | 0666);
    if (msqid == -1) { perror("msgget"); exit(1); }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        /* CHILD */
        child_loop(n, k, r, msqid);
        exit(0);
    } else {
        /* PARENT */
        g_msqid   = msqid;
        g_childpid = pid;
        signal(SIGINT, sigint_handler);

        parent_loop(msqid, pid);
    }

    return 0;
}
