#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>

// no globals, gcd() just takes params and main() has its own locals
int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int main(void) {
    int arr[] = {18, 24, 35, 49, 10, 63, 27, 40, 14, 21};
    int n = sizeof(arr) / sizeof(arr[0]);   // n = 10, always even
    int rounds = n / 2;

    int p2c[2];  // parent to child, sends {x, y}
    int c2p[2];  // child to parent, sends {g}

    if (pipe(p2c) == -1 || pipe(c2p) == -1) {
        perror("pipe");
        exit(1);
    }

    srand((unsigned int)time(NULL));

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        // ---------------- CHILD ----------------
        close(p2c[1]);   // child doesn't write to p2c
        close(c2p[0]);   // child doesn't read c2p

        for (int i = 0; i < rounds; i++) {
            int buf[2];
            ssize_t r = read(p2c[0], buf, sizeof(buf));
            if (r <= 0) break;   // pipe closed or parent died, just stop

            int x = buf[0], y = buf[1];
            int g = gcd(x, y);
            printf("[Child]  x=%d y=%d gcd=%d\n", x, y, g);
            fflush(stdout);

            long sleep_ms = (long)(time(NULL) % g);
            if (sleep_ms > 0) usleep((useconds_t)(sleep_ms * 1000));

            write(c2p[1], &g, sizeof(g));
        }

        close(p2c[0]);
        close(c2p[1]);
        exit(0);

    } else {
        // ---------------- PARENT ----------------
        close(p2c[0]);   // parent doesn't read p2c
        close(c2p[1]);   // parent doesn't write c2p

        int local_arr[10];
        for (int i = 0; i < n; i++) local_arr[i] = arr[i];
        int remaining = n;

        for (int i = 0; i < rounds; i++) {
            // grab first number, swap with last and shrink array
            int idx1 = rand() % remaining;
            int x = local_arr[idx1];
            local_arr[idx1] = local_arr[remaining - 1];
            remaining--;

            // grab second number same way, guaranteed different index
            int idx2 = rand() % remaining;
            int y = local_arr[idx2];
            local_arr[idx2] = local_arr[remaining - 1];
            remaining--;

            printf("[Parent] Sending x=%d y=%d\n", x, y);
            fflush(stdout);

            int buf[2] = {x, y};
            write(p2c[1], buf, sizeof(buf));

            int g;
            ssize_t r = read(c2p[0], &g, sizeof(g));
            if (r <= 0) break;

            printf("[Parent] Received g=%d\n", g);
            fflush(stdout);

            usleep((useconds_t)(g * 1000));
        }

        close(p2c[1]);
        close(c2p[0]);
        wait(NULL);   // reap the child, don't leave a zombie around
    }

    return 0;
}