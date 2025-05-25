// selectを用いた複数クライアントからの接続を受け付ける
#include <netdb.h>
#include "exp1.h"
#include "exp1lib.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#define MAXCHILD 1200 // 同時接続上限
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
void *thread_func(void *arg);

int main(int argc, char **argv)
{
    int sock_listen;

    sock_listen = exp1_tcp_listen(10018);

    acceptLoop(sock_listen);
}

void acceptLoop(int fd)
{
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
            getnameinfo((struct sockaddr *)&from, len, hbuf,
                        sizeof(hbuf), sbuf, sizeof(sbuf),
                        NI_NUMERICHOST | NI_NUMERICSERV);
            fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);

            // スレッド生成
            int *param = (int *)malloc(sizeof(int)); // ヒープより領域を確保
            *param = acc;                            // 確保した領域にクライアントソケットを格納
            pthread_t th;
            if (
                pthread_create(             /*正常終了時に返り値0*/
                               &th,         /*スレッドIDへのポインタ*/
                               NULL,        /*スレッド属性オブジェクトの指定 NULL->デフォルト値*/
                               thread_func, /*スレッドにて実行されるルーチン*/
                               param        /*start_routineへの単一の引数 クライアントソケットを渡す*/
                               ) != 0)
            {
                perror("pthread_create");
                close(acc); // 切断
            }
        }
    }
}

void *thread_func(void *arg)
{

    int *accP = (int *)arg;
    int acc = *accP;
    free(accP);
    pthread_detach(pthread_self()); // 現在使用しているスレッドをデタッチ状態にする
    exp1_http_session(acc);
    close(acc);
    pthread_exit(NULL);
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
        case PARSE_END:
            return 1;
        }
    }
    return 0;
}

void exp1_parse_status(char *status, exp1_info_type *pinfo)
{
    char cmd[1024];
    char path[1024];
    // char *pext;
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
        case SEARCH_END:
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
