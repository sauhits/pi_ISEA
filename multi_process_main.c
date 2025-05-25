#include "exp1.h"
#include "exp1lib.h"
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>

#define NI_MAXHOST 1025
#define NI_MAXSERV 32

typedef struct
{
    char cmd[64];
    char path[256];
    char real_path[256];
    char type[64];
    int code;
    int size;
} exp1_info_type;

// プロトタイプ宣言
int exp1_http_session(int sock);
int exp1_parse_header(char *buf, int size, exp1_info_type *info);
void exp1_parse_status(char *status, exp1_info_type *pinfo);
void exp1_check_file(exp1_info_type *info);
void exp1_http_reply(int sock, exp1_info_type *info);
void exp1_send_404(int sock);
void exp1_send_file(int sock, char *filename);
void acceptLoop(int fd);
void sigChldHandler(int sig);
void printChildProcessStatus(pid_t pid, int status);

int main(int argc, char **argv)
{
    int sock_listen;

    sock_listen = exp1_tcp_listen(10018);
    if (sock_listen < 0)
    {
        perror("exp1_tcp_listen");
        exit(EXIT_FAILURE);
    }
    acceptLoop(sock_listen);
    while (1)
    {
        struct sockaddr addr;
        int sock_client;
        int len;

        sock_client = accept(sock_listen, &addr, (socklen_t *)&len);
        exp1_http_session(sock_client);

        shutdown(sock_client, SHUT_RDWR);
        close(sock_client);
    }
}

void acceptLoop(int fd)
{
    // 子プロセス終了時のシグナルハンドラを指定
    struct sigaction sa;
    sigaction(SIGCHLD, NULL, &sa);
    sa.sa_handler = sigChldHandler;
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGCHLD, &sa, NULL);

    while (1)
    {
        struct sockaddr_storage from;
        socklen_t len = sizeof(from);
        int acc = 0;
        if ((acc = accept(fd, (struct sockaddr *)&from, &len)) == -1)
        {
            // エラー処理
            if (errno != EINTR)
            {
                perror("accept");
            }
        }
        else
        {
            // クライアントからの接続が行われた場合
            char hbuf[NI_MAXHOST];
            char sbuf[NI_MAXSERV];
            getnameinfo((struct sockaddr *)&from, len, hbuf, sizeof(hbuf),
                        sbuf, sizeof(sbuf),
                        NI_NUMERICHOST | NI_NUMERICSERV);
            fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);

            // プロセス生成
            pid_t pid = fork();
            if (pid == -1)
            {
                // エラー処理
                perror("fork");
                close(acc);
            }
            else if (pid == 0)
            {
                // 子プロセス
                close(fd); // サーバソケットクローズ
                exp1_http_session(acc);
                shutdown(fd, SHUT_RDWR);
                close(acc); // アクセプトソケットクローズ
                acc = -1;
                _exit(1);
            }
            else
            {
                // 親プロセス
                close(acc); // アクセプトソケットクローズ
                acc = -1;
            }

            // 子プロセスの終了処理
            int status = -1;
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            {
                // 終了した(かつSIGCHLDでキャッチできなかった)子プロセスが存在する場合
                // WNOHANGを指定してあるのでノンブロッキング処理
                // 各子プロセス終了時に確実に回収しなくても新規クライアント接続時に回収できれば十分なため．
                printChildProcessStatus(pid, status);
            }
        }
    }
}

int exp1_http_session(int sock)
{
    char buf[2048];
    int recv_size = 0;
    exp1_info_type info;
    int ret = 0;

    while (ret == 0)
    {
        int size = recv(sock, buf + recv_size, 2048, 0);

        if (size == -1)
        {
            return -1;
        }

        recv_size += size;
        ret = exp1_parse_header(buf, recv_size, &info);
    }

    exp1_http_reply(sock, &info);

    return 0;
}

int exp1_parse_header(char *buf, int size, exp1_info_type *info)
{
    char status[1024];
    int i, j;

    enum state_type
    {
        PARSE_STATUS,
        PARSE_END
    } state;

    state = PARSE_STATUS;
    j = 0;
    for (i = 0; i < size; i++)
    {
        switch (state)
        {
        case PARSE_STATUS:
            if (buf[i] == '\r')
            {
                status[j] = '\0';
                j = 0;
                state = PARSE_END;
                exp1_parse_status(status, info);
                exp1_check_file(info);
            }
            else
            {
                status[j] = buf[i];
                j++;
            }
            break;
        }

        if (state == PARSE_END)
        {
            return 1;
        }
    }

    return 0;
}

void exp1_parse_status(char *status, exp1_info_type *pinfo)
{
    char cmd[1024];
    char path[1024];
    char *pext;
    int i, j;

    enum state_type
    {
        SEARCH_CMD,
        SEARCH_PATH,
        SEARCH_END
    } state;

    state = SEARCH_CMD;
    j = 0;
    for (i = 0; i < strlen(status); i++)
    {
        switch (state)
        {
        case SEARCH_CMD:
            if (status[i] == ' ')
            {
                cmd[j] = '\0';
                j = 0;
                state = SEARCH_PATH;
            }
            else
            {
                cmd[j] = status[i];
                j++;
            }
            break;
        case SEARCH_PATH:
            if (status[i] == ' ')
            {
                path[j] = '\0';
                j = 0;
                state = SEARCH_END;
            }
            else
            {
                path[j] = status[i];
                j++;
            }
            break;
        }
    }

    strcpy(pinfo->cmd, cmd);
    strcpy(pinfo->path, path);
}

void exp1_check_file(exp1_info_type *info)
{
    struct stat s;
    int ret;
    char *pext;

    // 公開ディレクトリの指定
    sprintf(info->real_path, "/home/pi/SU-html%s", info->path);
    ret = stat(info->real_path, &s);

    if (S_ISDIR(s.st_mode))
    // if (ret == 0 && S_ISDIR(s.st_mode))
    {
        sprintf(info->real_path, "%s/index.html", info->real_path);
    }

    ret = stat(info->real_path, &s);

    if (ret == -1)
    {
        info->code = 404;
    }
    else
    {
        info->code = 200;
        info->size = (int)s.st_size;
    }

    pext = strstr(info->path, ".");
    if (pext != NULL && strcmp(pext, ".html") == 0)
    {
        strcpy(info->type, "text/html");
    }
    else if (pext != NULL && strcmp(pext, ".jpg") == 0)
    {
        strcpy(info->type, "image/jpeg");
    }
}

void exp1_http_reply(int sock, exp1_info_type *info)
{
    char buf[16384];
    int len;
    int ret;

    if (info->code == 404)
    {
        exp1_send_404(sock);
        printf("404 not found %s\n", info->path);
        return;
    }

    len = sprintf(buf, "HTTP/1.0 200 OK\r\n");
    len += sprintf(buf + len, "Content-Length: %d\r\n", info->size);
    len += sprintf(buf + len, "Content-Type: %s\r\n", info->type);
    len += sprintf(buf + len, "\r\n");

    ret = send(sock, buf, len, 0);
    if (ret < 0)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return;
    }

    exp1_send_file(sock, info->real_path);
}

void exp1_send_404(int sock)
{
    char buf[16384];
    int ret;

    sprintf(buf, "HTTP/1.0 404 Not Found\r\n\r\n");
    printf("%s", buf);
    ret = send(sock, buf, strlen(buf), 0);

    if (ret < 0)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }
}

void exp1_send_file(int sock, char *filename)
{
    FILE *fp;
    int len;
    char buf[16384];

    fp = fopen(filename, "r");
    if (fp == NULL)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return;
    }

    len = fread(buf, sizeof(char), 16384, fp);
    while (len > 0)
    {
        int ret = send(sock, buf, len, 0);
        if (ret < 0)
        {
            shutdown(sock, SHUT_RDWR);
            close(sock);
            break;
        }
        len = fread(buf, sizeof(char), 1460, fp);
    }

    fclose(fp);
}

// シグナルハンドラによって子プロセスのリソースを回収する
void sigChldHandler(int sig)
{
    // 子プロセスの終了を待つ
    int status = -1;
    pid_t pid = wait(&status);

    // 非同期シグナルハンドラ内でfprintfを使うことは好ましくないが，
    // ここではプロセスの状態表示に簡単のため使うことにする
    printChildProcessStatus(pid, status);
}

void printChildProcessStatus(pid_t pid, int status)
{
    fprintf(stderr, "sig_chld_handler:wait:pid=%d,status=%d\n", pid, status);
    fprintf(stderr, "  WIFEXITED:%d,WEXITSTATUS:%d,WIFSIGNALED:%d,"
                    "WTERMSIG:%d,WIFSTOPPED:%d,WSTOPSIG:%d\n",
            WIFEXITED(status),
            WEXITSTATUS(status), WIFSIGNALED(status), WTERMSIG(status),
            WIFSTOPPED(status),
            WSTOPSIG(status));
}