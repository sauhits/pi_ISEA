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
    return 0;
}

int exp1_http_session(int sock)
{
    // printf("exp1_http_session\n");
    char buf[2048];
    int recv_size = 0;
    exp1_info_type info;
    int ret = 0;

    while (ret <= 0)
    {
        int size = recv(sock, buf + recv_size, 2048, 0);

        if (size <= 0)
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
    // printf("exp1_parse_header\n");
    char status[1024];
    int i, j = 0;

    for (i = 0; i < size - 1; i++)
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
        {
            status[j] = '\0';
            exp1_parse_status(status, info);
            exp1_check_file(info);
            return 1;
        }

        if (j >= sizeof(status) - 1)
        {
            fprintf(stderr, "Status line too long\n");
            return -1;
        }

        status[j++] = buf[i];
    }

    return 0;
    
}


void exp1_parse_status(char *status, exp1_info_type *pinfo)
{
    // printf("exp1_parse_status\n");
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
    // printf("exp1_check_file\n");
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
    // printf("exp1_http_reply\n");
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

void acceptLoop(int fd)
{
    struct sockaddr addr;
    // クライアント管理配列の初期化
    int childNum = 0;
    int child[MAXCHILD];
    for (int i = 0; i < MAXCHILD; i++)
    {
        child[i] = -1;
    }

    while (1)
    {
        fd_set mask;
        FD_ZERO(&mask);
        FD_SET(fd, &mask);
        int width = fd + 1;
        int i = 0;

        for (i = 0; i < childNum; i++)
        {
            if (child[i] != -1)
            {
                FD_SET(child[i], &mask);
                if (width <= child[i])
                {
                    width = child[i] + 1;
                }
            }
        }

        fd_set ready = mask;
        // timeoutの設定
        struct timeval timeout;
        timeout.tv_sec = 600;
        timeout.tv_usec = 0;

        switch (select(width, (fd_set *)&ready, NULL, NULL, &timeout))
        {
        case -1:
            perror("select");
            break;
        case 0:
            break;
        default:
            if (FD_ISSET(fd, &ready))
            {
                {
                    /* data */
                };

                int acc = 0; // クライアントソケット
                socklen_t len = sizeof(addr);
                if ((acc = accept(fd, &addr, &len)) == -1)
                {
                    perror("accept");
                    continue;
                }
                char hbuf[NI_MAXHOST];
                char sbuf[NI_MAXSERV];
                getnameinfo(
                    &addr,
                    len,
                    hbuf,
                    sizeof(hbuf),
                    sbuf,
                    sizeof(sbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV);
                fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);

                // クライアント管理配列の更新
                int pos = -1;
                int i = 0;
                for (i = 0; i < childNum; i++)
                {
                    if (child[i] == -1) // 最後尾に新規追加
                    {
                        pos = i;
                        break;
                    }
                }

                if (pos == -1)
                {
                    if (childNum >= MAXCHILD)
                    {
                        fprintf(stderr, "child is full.\n");
                        close(acc);
                    }
                    else // 接続上限内で配列を拡張する
                    {
                        pos = childNum;
                        childNum = childNum + 1;
                    }
                }

                if (pos != -1)
                {
                    child[pos] = acc;
                }
            }

            // アクセプトしたソケットがレディの場合を確認する
            int i = 0;
            for (i = 0; i < childNum; i++)
            {
                if (child[i] != -1 && FD_ISSET(child[i], &ready))
                {
                    // printf("child[%d] is ready.\n", i);
                    exp1_http_session(child[i]);
                    shutdown(child[i], SHUT_RDWR);
                    close(child[i]);
                    child[i] = -1;
                }
            }
        }
    }
}
