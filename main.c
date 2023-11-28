#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define R 0 // 읽기
#define W 1 // 쓰기
#define TOTAL_NODE 4

int main(int argc, char *argv[]) {
    int pid_arr[4];

    int fd_to_parent[TOTAL_NODE][2];
    int fd_to_child[TOTAL_NODE][2];
    int fd_err[TOTAL_NODE][2];

    char *node_name[] = {"node1", "node2", "node3", "node4"};

    for (int node = 0; node < TOTAL_NODE; node++) {

        if (pipe(fd_to_parent[node]) == -1) { // 부모 방향 pipe
            perror("pipe In");
            exit(1);
        }

        if (pipe(fd_to_child[node]) == -1) { // 자식 방향 pipe
            perror("pipe Out");
            exit(1);
        }

        if (pipe(fd_err[node]) == -1) { // 에러 pipe
            perror("pipe Err");
            exit(1);
        }

        switch (pid_arr[node] = fork()) {
        case -1: // err
            perror("fork");
            exit(1);
            break;

        case 0: // child process
            close(fd_to_parent[node][R]);
            close(fd_to_child[node][W]);
            close(fd_err[node][R]);

            // 표준 입력은 자식쪽 단방향 pipe에서 읽어온다.
            dup2(fd_to_child[node][R], STDIN_FILENO);

            // 표준 출력은 부모쪽 단방향 pipe에 작성한다.
            dup2(fd_to_parent[node][W], STDOUT_FILENO);

            // 표준 에러는 부모쪽에러 pipe에 작성한다.
            dup2(fd_err[node][W], STDERR_FILENO);

            char *exec_argv[] = {"sshpass",
                                 "-p",
                                 "ubuntu",
                                 "ssh",
                                 node_name[node],
                                 "-T",
                                 "-o",
                                 "StrictHostKeyChecking=no",
                                 "-l",
                                 "ubuntu",
                                 NULL};
            execv("/bin/sshpass", exec_argv);
            break;

        default: // parent process
            close(fd_to_parent[node][W]);
            close(fd_to_child[node][R]);
            close(fd_err[node][W]);
        }
    }
}