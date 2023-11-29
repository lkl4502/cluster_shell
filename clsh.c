#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MSGSIZE 1024
#define MAXWORD 20
#define TOTAL_NODE 4

int split(char input[], char result[][MSGSIZE], const char *delimiter) {
    int word_cnt = 0;
    char *ptr = strtok(input, delimiter);
    while (ptr != NULL) {
        strcpy(result[word_cnt++], ptr);
        ptr = strtok(NULL, delimiter);
    }
    return word_cnt;
}

int extract_command(char *input[MAXWORD], char result[], int start, int end) {
    int idx = 0;
    for (int i = start; i < end; i++) {
        strcat(result, input[i]);
        idx += strlen(input[i]);
        result[idx++] = ' ';
    }
    result[idx - 1] = '\n';
    return idx;
}

int main(int argc, char *argv[]) {
    char buf[MSGSIZE] = {0};
    pid_t pid_arr[TOTAL_NODE];

    int fd_to_child[TOTAL_NODE][2];
    int fd_to_parent[TOTAL_NODE][2];
    int fd_to_err[TOTAL_NODE][2];

    char *node_name[TOTAL_NODE] = {"node1", "node2", "node3", "node4"};

    for (int node = 0; node < TOTAL_NODE; node++) {
        if (pipe(fd_to_parent[node]) == -1) {
            perror("pipe parent");
            exit(1);
        }

        if (pipe(fd_to_child[node]) == -1) {
            perror("pipe child");
            exit(1);
        }

        if (pipe(fd_to_err[node]) == -1) {
            perror("pipe err");
            exit(1);
        }

        if (fcntl(fd_to_parent[node][0], F_SETFL, O_NONBLOCK) == -1) {
            perror("fcntl");
            exit(1);
        }

        switch (pid_arr[node] = fork()) {
        case -1:
            perror("fork");
            exit(1);
            break;
        case 0: // child
            close(fd_to_child[node][1]);
            close(fd_to_parent[node][0]);
            close(fd_to_err[node][0]);

            dup2(fd_to_child[node][0], STDIN_FILENO);
            dup2(fd_to_parent[node][1], STDOUT_FILENO);
            dup2(fd_to_err[node][1], STDERR_FILENO);

            setvbuf(stdin, NULL, _IOLBF, 0);
            setvbuf(stdout, NULL, _IOLBF, 0);

            char *ssh_argv[] = {"sshpass",
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
            if (execv("/bin/sshpass", ssh_argv) == -1) {
                perror("execv");
                exit(1);
            }
            break;

        default: // parent
            close(fd_to_child[node][0]);
            close(fd_to_parent[node][1]);
            close(fd_to_err[node][1]);

            setvbuf(stdin, NULL, _IOLBF, 0);
            setvbuf(stdout, NULL, _IOLBF, 0);
            break;
        }
    }

    char input_node[MAXWORD][MSGSIZE] = {0};
    char command[MSGSIZE] = {0};
    int command_len;
    int input_node_num;
    int n;

    if (argc == 1) {
        perror("인자 부족");
        return 0;
    }

    // -b 옵션 확인
    bool redirection_flag = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-b"))
            redirection_flag = true;

    // -b 옵션시에 명령어 추출 위치 변경
    int start = 0;
    if (redirection_flag) {
        start = 1;
    }

    if (!strcmp(argv[1], "-h")) { // -h옵션
        input_node_num = split(argv[2], input_node, ",");
        command_len = extract_command(argv, command, start + 3, argc);

    } else if (!strcmp(argv[1], "--hostfile")) { // --hostfile 옵션
        int hostfile_fd;

        char absolute_path[260] = {0};
        realpath(argv[2], absolute_path);
        if ((hostfile_fd = open(absolute_path, O_RDONLY)) == -1) {
            perror("Open");
            exit(1);
        }

        if ((n = read(hostfile_fd, buf, MSGSIZE)) == -1) {
            perror("Read");
            exit(1);
        }

        close(hostfile_fd);

        input_node_num = split(buf, input_node, "\n");
        memset(buf, 0, sizeof(buf));
        command_len = extract_command(argv, command, start + 3, argc);

    } else { // --hostfile 생략
        char *val;
        char explanation[MSGSIZE] = {0};
        int hostfile_fd;
        if ((val = getenv("CLSH_HOSTS")) != NULL) { // CLSH_HOSTS 환경변수
            input_node_num = split(val, input_node, ":");
            command_len = extract_command(argv, command, start + 1, argc);
            sprintf(explanation, "Note: use CLSH_HOSTS environment\n");
        } else if ((val = getenv("CLSH_HOSTFILE")) != NULL) {
            // CLSH_HOSTFILE 환경 변수
            if ((hostfile_fd = open(val, O_RDONLY)) == -1) {
                perror("Open");
                exit(1);
            }

            if ((n = read(hostfile_fd, buf, MSGSIZE)) == -1) {
                perror("Read");
                exit(1);
            }

            close(hostfile_fd);

            input_node_num = split(buf, input_node, "\n");
            memset(buf, 0, sizeof(buf));
            command_len = extract_command(argv, command, start + 1, argc);
            sprintf(explanation,
                    "Note: use hostfile \'%s\' (CLSH_HOSTFILE env)\n", val);
        } else if ((hostfile_fd = open(".hostfile", O_RDONLY)) != -1) {
            // .hostfile
            if ((n = read(hostfile_fd, buf, MSGSIZE)) == -1) {
                perror("Read");
                exit(1);
            }

            close(hostfile_fd);

            input_node_num = split(buf, input_node, "\n");
            memset(buf, 0, sizeof(buf));
            command_len = extract_command(argv, command, start + 1, argc);
            sprintf(val, "Note: use hostfile \'%s\' (default)\n", val);
        } else { // 모두 없을 때
            sprintf(explanation, "--hostfile 옵션이 제공되지 않았습니다.\n");
            printf("%s", explanation);
            return 0;
        }
        printf("%s", explanation);
    }

    if (redirection_flag) {
        char pipe_input[MSGSIZE] = {0};
        char *tmp;
        int idx = 0;
        while (1) {
            tmp = fgets(&pipe_input[idx], MSGSIZE, stdin);
            if (tmp == NULL)
                break;
            idx += strlen(tmp);
            pipe_input[idx - 1] = ' ';
        }

        command[command_len - 1] = ' ';
        for (int i = 0; i < 3; i++)
            command[command_len++] = '<';
        command[command_len++] = ' ';

        for (int i = 0; i < idx; i++)
            command[command_len++] = pipe_input[i];
        command[command_len++] = '\n';
    }

    bool check_response[TOTAL_NODE] = {true, true, true, true};

    for (int i = 0; i < input_node_num; i++) {
        for (int node = 0; node < TOTAL_NODE; node++) {
            if (!strcmp(input_node[i], node_name[node])) {
                check_response[node] = false;
                write(fd_to_child[node][1], command, command_len);
                break;
            }
        }
    }

    while (1) {
        for (int node = 0; node < TOTAL_NODE; node++) {
            if (check_response[node])
                continue;

            switch (n = read(fd_to_parent[node][0], buf, MSGSIZE)) {
            case -1:
                if (errno != EAGAIN)
                    perror("read");
                break;

            case 0:
                perror("EOF");
                break;

            default:
                printf("%s: %s\n", node_name[node], buf);
                memset(buf, 0, n);
                check_response[node] = true;
                break;
            }
        }

        bool flag = true;
        for (int i = 0; i < TOTAL_NODE; i++)
            if (!check_response[i])
                flag = false;
        if (flag)
            break;
    }

    return 0;
}