#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MSGSIZE 1024
#define MAXWORD 20
#define TOTAL_NODE 4

pid_t pid_arr[TOTAL_NODE];
int terminate_order[TOTAL_NODE];
int terminate_cnt = 0;

char *node_name[TOTAL_NODE] = {"node1", "node2", "node3", "node4"};

int fd_to_child[TOTAL_NODE][2];
int fd_to_parent[TOTAL_NODE][2];
int fd_to_err[TOTAL_NODE][2];

char input_node[MAXWORD][MSGSIZE] = {0};
int input_node_num;
bool process_terminated = false;
bool redirection_flag = false;
bool interactive_mode = false;

void signal_to_child(int eventnum, void *signo) {
    signal(*(int *)signo, SIG_IGN);
    kill(getpgid(getpid()) * -1, *(int *)signo);
}

void wait_all_child() {
    pid_t child_pid;
    for (int i = 0; i < TOTAL_NODE; i++) {
        if ((child_pid = wait(NULL)) == -1)
            break;
        else {
            for (int node = 0; node < TOTAL_NODE; node++) {
                if (pid_arr[node] == child_pid) {
                    printf("%s(%d) : Terminated.\n", node_name[node],
                           child_pid);
                    break;
                }
            }
        }
    }
    printf("All processes are terminated.\n");
}

void term_signal_handler(int signo) {
    process_terminated = true;
    psignal(signo, "Signal ");
    if (atexit(wait_all_child) == -1) {
        perror("atexit call");
        exit(1);
    }

    if (on_exit(signal_to_child, &signo) == -1) {
        perror("on_exit call");
        exit(1);
    }
}

void exit_signal_handler(int signo) {
    process_terminated = true;
    psignal(signo, "Signal ");
    signal_to_child(0, &signo);
    wait_all_child();
    exit(0);
}

void sigchild_handler(int signo) {
    if (process_terminated)
        return;
    // 먼저 종료된 자식의 pid와 종료 상태
    int status, i;
    pid_t child_pid = wait(&status);

    for (i = 0; i < TOTAL_NODE; i++)
        if (pid_arr[i] == child_pid)
            break;

    if (!WIFEXITED(status)) {
        signo = SIGTERM;
        printf("ERROR : %s connection lost\n", node_name[i]);
        signal_to_child(0, &signo);
        wait_all_child();
        exit(0);
    } else {
        terminate_order[terminate_cnt++] = i;
    }

    if (terminate_cnt == input_node_num) {
        for (i = 0; i < input_node_num; i++)
            printf("%s(%d) : Terminated.\n", node_name[terminate_order[i]],
                   pid_arr[terminate_order[i]]);
        printf("All processes are terminated.\n");
    }
}

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
    return idx;
}

int add_eof(char result[], int idx) {
    strcat(result, "&& echo eof\n");
    idx += 13;
    return idx;
}

char *concat(char strto[], const char strfrom[], char newstr[]) {
    int tlen;
    for (tlen = 0; strto[tlen] != '\0'; tlen++)
        newstr[tlen] = strto[tlen];
    for (int i = 0; strfrom[i] != '\0'; i++, tlen++)
        newstr[tlen] = strfrom[i];
    newstr[tlen] = '\0';
    return newstr;
}

bool decide_flag(bool response[TOTAL_NODE]) {
    bool flag = true;
    for (int i = 0; i < TOTAL_NODE; i++)
        if (!response[i]) {
            flag = false;
            break;
        }
    return flag ? true : false;
}

bool ssh_connect(char *command, bool check_response[TOTAL_NODE]) {
    for (int i = 0; i < input_node_num; i++) {
        for (int node = 0; node < TOTAL_NODE; node++) {
            if (!strcmp(input_node[i], node_name[node])) {
                check_response[node] = false;
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

                if (fcntl(fd_to_err[node][0], F_SETFL, O_NONBLOCK) == -1) {
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
                    close(fd_to_child[node][0]);

                    dup2(fd_to_parent[node][1], STDOUT_FILENO);
                    close(fd_to_parent[node][1]);

                    dup2(fd_to_err[node][1], STDERR_FILENO);
                    close(fd_to_err[node][1]);

                    setvbuf(stdin, NULL, _IOLBF, 0);
                    setvbuf(stdout, NULL, _IOLBF, 0);
                    setvbuf(stderr, NULL, _IOLBF, 0);

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
                                        command,
                                        NULL};

                    if (execv("/bin/sshpass", ssh_argv) == -1) {
                        perror("execv");
                        exit(1);
                    }

                    perror("execv call");
                    exit(1);
                    break;

                default: // parent
                    close(fd_to_child[node][0]);
                    close(fd_to_parent[node][1]);
                    close(fd_to_err[node][1]);

                    setvbuf(stdin, NULL, _IOLBF, 0);
                    setvbuf(stdout, NULL, _IOLBF, 0);
                    break;
                }
                break;
            }
        }
    }
    // SIGTERM -> 하던 일 하고 종료, SIGQUIT -> 바로 종료,
    // SIGINT
    struct sigaction act, child;

    sigemptyset(&act.sa_mask);
    sigemptyset(&child.sa_mask);

    act.sa_flags = 0;
    act.sa_handler = term_signal_handler;
    if (sigaction(SIGTERM, &act, 0) < 0) { // 정상종료
        perror("Sigaction SIGTERM");
        exit(1);
    }

    act.sa_handler = exit_signal_handler;
    if (sigaction(SIGQUIT, &act, 0) < 0) { // ctrl+\.
        perror("Sigaction SIGQUIT");
        exit(1);
    }

    if (sigaction(SIGINT, &act, 0) < 0) { // ctrl + c
        perror("Sigaction SIGINT");
        exit(1);
    }

    if (sigaction(SIGTSTP, &act, 0) < 0) { // ctrl + z
        perror("Sigaction SIGTSTP");
        exit(1);
    }

    child.sa_flags = SA_NOCLDSTOP;
    sigfillset(&child.sa_mask);
    child.sa_handler = sigchild_handler;
    if (sigaction(SIGCHLD, &child, 0) < 0) {
        perror("Sigaction child");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    char buf[MSGSIZE] = {0};

    char command[MSGSIZE] = {0};
    int command_len;
    int n;

    if (argc == 1) {
        perror("인자 부족");
        return 0;
    }

    // 옵션 확인
    char *out_file = NULL;
    char *err_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b")) {
            redirection_flag = true;
            continue;
        }
        if (strstr(argv[i], "--out=") != NULL) {
            out_file = strstr(argv[i], "--out=") + strlen("--out=");
            if (out_file[0] != '/') {
                out_file = strcat(realpath(out_file, NULL), "/");
            }
            continue;
        }
        if (strstr(argv[i], "--err=") != NULL) {
            err_file = strstr(argv[i], "--err=") + strlen("--err=");
            if (err_file[0] != '/') {
                err_file = strcat(realpath(err_file, NULL), "/");
            }
            continue;
        }

        if (!strcmp(argv[i], "-i")) {
            interactive_mode = true;
            continue;
        }
    }

    // 옵션에 따른 명령어 추출 위치 변경
    int start = 0;

    if (redirection_flag)
        start += 1;

    if (out_file != NULL)
        start += 1;

    if (err_file != NULL)
        start += 1;

    if (interactive_mode)
        start += 1;

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
            val = ".hostfile";
            input_node_num = split(buf, input_node, "\n");
            memset(buf, 0, sizeof(buf));
            command_len = extract_command(argv, command, start + 1, argc);
            sprintf(explanation, "Note: use hostfile \'%s\' (default)\n", val);
        } else { // 모두 없을 때
            sprintf(explanation, "--hostfile 옵션이 제공되지 않았습니다.\n");
            printf("%s", explanation);
            return 0;
        }
        printf("%s", explanation);
    }

    char interactive_buf[MSGSIZE] = {0};
    bool check_response[TOTAL_NODE] = {true, true, true, true};

    if (interactive_mode) {
        if (command_len == 0) { // 내부 쉘 동작
            printf("Enter 'quit' to leave this interactive mode\n");
            printf("Working with nodes : ");
            for (int i = 0; i < input_node_num; i++)
                if (i == input_node_num - 1)
                    printf("%s\n", input_node[i]);
                else
                    printf("%s, ", input_node[i]);

            ssh_connect("", check_response);

            while (1) { // 명령어 입력
                printf("clsh> ");
                fgets(interactive_buf, MSGSIZE, stdin);

                if (!strcmp(interactive_buf, "quit\n")) { // 종료
                    printf("Interactive Mode Exit\n");
                    break;
                }

                if (interactive_buf[0] == '!') { // 로컬 실행
                    printf("-------------------\n");
                    fprintf(stderr, "LOCAL : \n");
                    int res;
                    if (!strncmp(interactive_buf + 1, "cd", 2)) {
                        interactive_buf[strlen(interactive_buf) - 1] = '\0';
                        char *path = realpath(interactive_buf + 4, NULL);
                        res = chdir(path);
                    } else {
                        struct sigaction child, old;
                        sigemptyset(&child.sa_mask);
                        child.sa_flags = 0;
                        if (sigaction(SIGCHLD, &child, &old) < 0) {
                            perror("Sigaction child");
                            exit(1);
                        }
                        res = system(interactive_buf + 1);
                        if (sigaction(SIGCHLD, &old, 0) < 0) {
                            perror("Sigaction child");
                            exit(1);
                        }
                    }
                    if (res == -1) {
                        perror("System");
                        exit(1);
                    }
                    printf("-------------------\n");
                    continue;
                }

                interactive_buf[strlen(interactive_buf) - 1] = ' ';
                add_eof(interactive_buf, strlen(interactive_buf));

                char err_buf[MSGSIZE] = {0};
                bool check_err_response[TOTAL_NODE] = {true, true, true, true};

                for (int i = 0; i < input_node_num; i++) {
                    for (int node = 0; node < TOTAL_NODE; node++) {
                        if (!strcmp(input_node[i], node_name[node])) {
                            check_response[node] = false;
                            check_err_response[i] = false;
                            write(fd_to_child[node][1], interactive_buf,
                                  strlen(interactive_buf));
                            break;
                        }
                    }
                }

                printf("-------------------\n");

                memset(interactive_buf, 0, MSGSIZE);
                int sleep_cnt = 0;
                while (1) {
                    for (int node = 0; node < TOTAL_NODE; node++) {
                        if (!check_response[node]) {
                            switch (n = read(fd_to_parent[node][0],
                                             interactive_buf, MSGSIZE)) {
                            case -1:
                                if (errno == EINTR || errno == EAGAIN) {
                                    usleep(500000);
                                    sleep_cnt++;
                                    if (sleep_cnt > 6) {
                                        printf("%s's Output: \n\n",
                                               node_name[node]);
                                        check_response[node] = true;
                                    }
                                    break;
                                } else {
                                    perror("Read");
                                    exit(1);
                                }

                            case 0:
                                check_response[node] = true;
                                break;

                            default:
                                char *eof_address;
                                if ((eof_address = strstr(interactive_buf,
                                                          "eof\n")) != NULL)
                                    memset(eof_address, 0, 5);

                                printf("%s's Output: \n%s\n", node_name[node],
                                       interactive_buf);

                                memset(interactive_buf, 0, n);
                                check_response[node] = true;
                                break;
                            }
                        }

                        if (!check_err_response[node]) {
                            int err_n;
                            switch (err_n = read(fd_to_err[node][0], err_buf,
                                                 MSGSIZE)) {
                            case -1:
                                if (errno == EINTR || errno == EAGAIN) {
                                    usleep(500000);
                                    sleep_cnt++;
                                    if (sleep_cnt > 7)
                                        check_err_response[node] = true;
                                    break;
                                } else {
                                    perror("Err Read");
                                    exit(1);
                                }
                            case 0:
                                check_err_response[node] = true;
                                break;
                            default:
                                printf("%s's Err: \n%s\n", node_name[node],
                                       err_buf);
                                memset(err_buf, 0, err_n);
                                check_err_response[node] = true;
                                break;
                            }
                        }
                    }

                    if (decide_flag(check_response) &&
                        decide_flag(check_err_response))
                        break;
                }

                printf("-------------------\n");
            }
            return 0;

        } else { // -i 옵션
            command_len = add_eof(command, command_len);
            ssh_connect(command, check_response);

            while (1) {
                bool tmp_response[TOTAL_NODE] = {true, true, true, true};
                for (int i = 0; i < TOTAL_NODE; i++) {
                    if (!check_response[i])
                        tmp_response[i] = false;
                }

                while (1) {
                    for (int node = 0; node < TOTAL_NODE; node++) {
                        if (tmp_response[node] || check_response[node])
                            continue;
                        switch (n = read(fd_to_err[node][0], interactive_buf,
                                         MSGSIZE)) {
                        case -1:
                            if (errno == EINTR || errno == EAGAIN) {
                                sleep(1);
                                break;
                            } else {
                                perror("Read");
                                exit(1);
                            }

                        case 0:
                            tmp_response[node] = true;
                            break;

                        default:
                            printf("%s: \n%s\n", node_name[node],
                                   interactive_buf);
                            memset(interactive_buf, 0, n);

                            int read_n = read(fd_to_parent[node][0],
                                              interactive_buf, MSGSIZE);

                            if (strstr(interactive_buf, "eof\n") == NULL) {
                                fgets(interactive_buf, MSGSIZE, stdin);
                                write(fd_to_child[node][1], interactive_buf,
                                      strlen(interactive_buf));
                            } else {
                                check_response[node] = true;
                            }
                            tmp_response[node] = true;
                            memset(interactive_buf, 0, strlen(interactive_buf));
                            break;
                        }
                    }

                    if (decide_flag(tmp_response)) {
                        break;
                    }
                }
                if (decide_flag(check_response))
                    break;
            }
        }
        return 0;
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
        command[command_len++] = ' ';
    }

    bool check_err_response[TOTAL_NODE] = {true, true, true, true};
    command_len = add_eof(command, command_len);
    ssh_connect(command, check_response);

    for (int i = 0; i < TOTAL_NODE; i++)
        if (!check_response[i])
            check_err_response[i] = false;
    char err_buf[MSGSIZE] = {0};
    int err_n;

    while (1) {
        for (int node = 0; node < TOTAL_NODE; node++) {
            if (!check_response[node]) {
                switch (n = read(fd_to_parent[node][0], buf, MSGSIZE)) {
                case -1:
                    if (errno == EINTR || errno == EAGAIN) {
                        break;
                    } else {
                        perror("Read");
                        exit(1);
                    }

                case 0:
                    check_response[node] = true;
                    break;

                default:
                    char *eof_address;
                    if ((eof_address = strstr(buf, "eof\n")) != NULL) {
                        memset(eof_address, 0, 5);
                    }
                    if (out_file != NULL) {
                        char file_name[MSGSIZE] = {0};

                        concat(out_file, node_name[node], file_name);
                        concat(file_name, ".out", file_name);

                        int out_fd =
                            open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (out_fd == -1) {
                            perror("Open");
                            exit(1);
                        }

                        write(out_fd, buf, n);
                        close(out_fd);
                    } else
                        printf("%s's Output: \n%s\n", node_name[node], buf);

                    memset(buf, 0, n);
                    check_response[node] = true;
                    break;
                }
            }

            if (!check_err_response[node]) {
                switch (err_n = read(fd_to_err[node][0], err_buf, MSGSIZE)) {
                case -1:
                    if (errno == EINTR || errno == EAGAIN) {
                        break;
                    } else {
                        perror("Err Read");
                        exit(1);
                    }
                case 0:
                    check_err_response[node] = true;
                    break;
                default:
                    if (err_file != NULL) {
                        char file_name[MSGSIZE] = {0};
                        concat(err_file, node_name[node], file_name);
                        concat(file_name, ".err", file_name);

                        int err_fd =
                            open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (err_fd == -1) {
                            perror("Open");
                            exit(1);
                        }

                        write(err_fd, err_buf, err_n);
                        close(err_fd);
                    } else {
                        printf("%s's Err: \n%s\n", node_name[node], err_buf);
                        memset(err_buf, 0, err_n);
                    }
                    check_err_response[node] = true;
                    break;
                }
            }
        }

        if (decide_flag(check_response) && decide_flag(check_err_response))
            break;
    }
    if (input_node_num != terminate_cnt)
        pause();
    return 0;
}
