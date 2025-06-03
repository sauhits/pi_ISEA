#include "exp1.h"

typedef struct
{
    char cmd[64];
    char path[256];
    char real_path[256];
    char type[64];
    int code;
    int size;
} exp1_info_type;

int exp1_tcp_listen(int);
int exp1_http_session(int);
int exp1_parse_header(char *, int, exp1_info_type *);
void exp1_parse_status(char *, exp1_info_type *);
void exp1_check_file(exp1_info_type *);
void exp1_http_reply(int, exp1_info_type *);
void exp1_send_404(int);
void exp1_send_file(int, char *);

int main(int argc, char **argv)
{
    int sock_listen;
    int port;

    if (argc != 2)
    {
        printf("usage: %s [port]\n", argv[0]);
        exit(-1);
    }

    port = atoi(argv[1]);

    sock_listen = exp1_tcp_listen(port);

    while (1)
    {
        // struct sockaddr addr;
        int sock_client;
        // int len;

        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        sock_client = accept(sock_listen, (struct sockaddr *)&addr, &len);

        // sock_client = accept(sock_listen, &addr, (socklen_t*) &len);
        exp1_http_session(sock_client);

        shutdown(sock_client, SHUT_RDWR);
        close(sock_client);
    }

    close(sock_listen);
    return 0;
}

int exp1_http_session(int sock)
{
    char buf[2048];
    int recv_size = 0;
    exp1_info_type info;
    int ret = 0;
    printf("%p\n\n", buf);

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
    printf("[*]exp1_parse_header: size=%d\n", size);
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
                // printf("%s",status);
                exp1_parse_status(status, info);
                exp1_check_file(info);
            }
            else
            {
                printf("else: %d\n", j);
                status[j] = buf[i];
                j++;
            }
            break;
        }

        if (state == PARSE_END)
        {
            printf("return 1\n");
            return 1;
        }
    }
    printf("return 0\n");
    return 0;
}

void exp1_parse_status(char *status, exp1_info_type *pinfo)
{
    printf("[*]exp1_parse_status: %s\n", status);
    char cmd[1024];
    char path[1024];
    char *pext;
    unsigned int i, j;

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

    printf("[*]exp1_parse_status: cmd=%s, path=%s\n", cmd, path);
    strcpy(pinfo->cmd, cmd);
    strcpy(pinfo->path, path);
    printf("[*]exp1_parse_status: pinfo->cmd=%s, pinfo->path=%s\n", pinfo->cmd, pinfo->path);
}

void exp1_check_file(exp1_info_type *info)
{
    struct stat s;
    int ret;
    char *pext;

    sprintf(info->real_path, "html%s", info->path);
    ret = stat(info->real_path, &s);

    if ((s.st_mode & S_IFMT) == S_IFDIR)
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
