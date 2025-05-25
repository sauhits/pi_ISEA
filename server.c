#include "exp1.h"
#include "exp1lib.h"
#include <sys/stat.h>
#include <string.h> // strcpy, strlen, strcmp, strstr を使用するために追加
#include <stdio.h>  // sprintf を使用するために追加

typedef enum
{
  CODE_301,
  CODE_302,
  CODE_303,
  CODE_307,
  CODE_308
} path_3XX;

#define PROTECT_PATH "/html/protect/"
#define AUTH_USER "admin"
#define AUTH_PASS "password"
#define BASE64_ENCODED_AUTH "YWRtaW46cGFzc3dvcmQ="

typedef struct
{
  char cmd[64];
  char path[256];
  char real_path[256];
  char type[64];
  int code;
  int size;
  char location[256];      // リダイレクト先のURLを格納
  char authorization[256]; // Authorizationヘッダーを格納
} exp1_info_type;

int exp1_tcp_listen(int port);
int exp1_http_session(int);
int exp1_parse_header(char *, int, exp1_info_type *);
void exp1_parse_status(char *, exp1_info_type *);
void exp1_check_file(exp1_info_type *);
void exp1_http_reply(int, exp1_info_type *);
void exp1_send_401(int, exp1_info_type *);
void exp1_send_404(int, exp1_info_type *);
void exp1_send_3XX(int, exp1_info_type *);
void exp1_send_file(int, char *);
path_3XX exp1_get_path_3XX(const char *path);

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
  // printf("%p\n\n", buf);

  while (ret == 0)
  {
    if ((sizeof(buf) - recv_size - 1) <= 0)
    {
      fprintf(stderr, "[!] Buffer overflow detected. Closing connection.\n");
      return -1;
    }
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

  char *auth_header_end;
  char *auth_header_start = strstr(buf, "\r\nAuthorization: Basic ");

  if (auth_header_start == NULL) // auth_start == NULL -> return
  {
    fprintf(stderr, "[!] Authorization header not found.\n");
    info->authorization[0] = '\0'; // Authorizationヘッダーがない場合は空文字列を設定
  }
  else
  {
    auth_header_start += strlen("\r\nAuthorization: Basic ");
    auth_header_end = strstr(auth_header_start, "\r\n");
    if (auth_header_end == NULL) // auth_end == NULL -> return
    {
      fprintf(stderr, "[!] Invalid Authorization header format.\n");
      return 0;
    }
    else
    {
      int len = auth_header_end - auth_header_start;
      if (len < sizeof(info->authorization))
      {
        strncpy(info->authorization, auth_header_start, len);
        info->authorization[len] = '\0';
      }
    }
  }

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
  // char *pext;
  unsigned int i, j;

  enum state_type
  {
    SEARCH_CMD,
    SEARCH_PATH,
    SEARCH_END
  } state;

  printf("status: %s\n", status);

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

  if (strcmp(info->path, PROTECT_PATH, strlen(PROTECT_PATH)) == 0)
  {
    if (info->authorization[0] == '\0') // authヘッダがない場合
    {
      ret = stat("html/401.html", &s);
      info->size = (int)s.st_size;
      info->code = 401; // Unauthorized
      strcpy(info->type, "text/html");
      return;
    }
    else
    {
      char decoded_auth[256];
      char user_pass_b64[256];
      strcpy(user_pass_b64, info->authorization);
      char expected_auth[256];
      snprintf(expected_auth, sizeof(expected_auth), "%s:%s", AUTH_USER, AUTH_PASS);

      if (strcmp(info->authorization, "") == 0)
      {
        fprintf(stderr, "[AUTH] Basic auth successful for path %s ! \n", info->path);
        // 認証成功
      }
      else
      {
        fprintf(stderr, "[AUTH] Basic auth failed for path %s ! \n", info->path);
        ret = stat("html/401.html", &s);
        info->size = (int)s.st_size;
        info->code = 401; // Unauthorized
        strcpy(info->type, "text/html");
        return;
      }
    }
  }

  switch (exp1_get_path_3XX(info->path)) // 3XX番台のパスをチェック
  {
  case CODE_301:
    info->code = 301;
    strcpy(info->type, "text/html");
    strcpy(info->location, "/index.html");
    return;

  case CODE_302:
    info->code = 302;
    strcpy(info->type, "text/html");
    strcpy(info->location, "/index.html");
    return;
  case CODE_303:
    info->code = 303;
    strcpy(info->type, "text/html");
    strcpy(info->location, "/index.html");
    return;
  case CODE_307:
    info->code = 307;
    strcpy(info->type, "text/html");
    strcpy(info->location, "/index.html");
    return;
  case CODE_308:
    info->code = 308;
    strcpy(info->type, "text/html");
    strcpy(info->location, "/index.html");
    return;
  default:
    break;
  }

  sprintf(info->real_path, "html%s", info->path);
  ret = stat(info->real_path, &s);

  if ((s.st_mode & S_IFMT) == S_IFDIR)
  {
    sprintf(info->real_path, "%s/index.html", info->real_path);
  }

  ret = stat(info->real_path, &s);

  if (ret == -1) // 404 Not Found
  {
    info->code = 404;
    stat("html/404.html", &s);
    info->size = (int)s.st_size;
    strcpy(info->real_path, "html/404.html");
    return;
  }
  else // 200 OK
  {
    info->code = 200;
    info->size = (int)s.st_size;
  }

  pext = strstr(info->path, ".");
  if (pext != NULL && strcmp(pext, ".html") == 0) // type/html
  {
    strcpy(info->type, "text/html");
  }
  else if (pext != NULL && strcmp(pext, ".jpg") == 0) // type/jpeg
  {
    strcpy(info->type, "image/jpeg");
  }
}

void exp1_http_reply(int sock, exp1_info_type *info)
{
  char buf[16384];
  int len;
  int ret;

  switch (info->code)
  {
  case 401:
    exp1_send_401(sock, info);
    return;
  case 404:
    exp1_send_404(sock, info);
    return;

  case 200:
    len = sprintf(buf, "HTTP/1.0 200 OK\r\n"
                       "Content-Length: %d\r\n"
                       "Content-Type: %s\r\n\r\n",
                  info->size, info->type);
    ret = send(sock, buf, len, 0);
    if (ret < 0)
    {
      shutdown(sock, SHUT_RDWR);
      close(sock);
      return;
    }
    exp1_send_file(sock, info->real_path);
    return;

  case 301:
    exp1_send_3XX(sock, info);
    return;
  case 302:
    exp1_send_3XX(sock, info);
    return;
  case 303:
    exp1_send_3XX(sock, info);
    return;
  case 307:
    exp1_send_3XX(sock, info);
    return;
  case 308:
    exp1_send_3XX(sock, info);
    return;
  }
}

void exp1_send_3XX(int sock, exp1_info_type *info)
{
  char buf[4096];
  char response_line[256];

  switch ((int)info->code)
  {
  case 301:
    strcpy(response_line, "301 Moved Permanently");
    printf("301 redirect %s to %s\n", info->path, info->location);
    info->size = 0;
    break;
  case 302:
    strcpy(response_line, "302 Found");
    printf("302 redirect %s to %s\n", info->path, info->location);
    info->size = 0;
    break;
  case 303:
    strcpy(response_line, "303 See Other");
    printf("303 redirect %s to %s\n", info->path, info->location);
    info->size = 0;
    break;
  case 307:
    strcpy(response_line, "307 Temporary Redirect");
    printf("307 redirect %s to %s\n", info->path, info->location);
    info->size = 0;
    break;
  case 308:
    strcpy(response_line, "308 Permanent Redirect");
    printf("308 redirect %s to %s\n", info->path, info->location);
    info->size = 0;
    break;
  default:
    break;
  }
  // 3XX番台
  snprintf(buf, sizeof(buf), "HTTP/1.0 %s\r\n"
                             "Location: %s\r\n"
                             "Content-Type: %s\r\n"
                             "Content-Length: %d\r\n"
                             "Connection: close\r\n"
                             "\r\n",
           response_line, info->location, info->type, info->size);

  if (send(sock, buf, strlen(buf), 0) < 0)
  {
    perror("send 3XX header");
    return;
  }
}

void exp1_send_401(int sock, exp1_info_type *info)
{
  char buf[1024];
  int len = snprintf(buf, sizeof(buf), "HTTP/1.0 401 Unauthorized\r\n"
                                       "WWW-Authenticate: Basic realm=\"Secure Area\"\r\n"
                                       "Content-Type: text/html\r\n"
                                       "Content-Length: %d\r\n"
                                       "Connection: close\r\n"
                                       "\r\n",
                     info->size);
  int ret = send(sock, buf, len, 0);
  if (ret < 0)
  {
    perror("send 401 header");
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return;
  }
  printf("Sending 401 page: %s\n", info->real_path);
  exp1_send_file(sock, info->real_path);
}

void exp1_send_404(int sock, exp1_info_type *info)
{
  char buf[16384];
  int ret;
  int len;

  printf("404 not found %s\n", info->path);
  len = sprintf(buf, "HTTP/1.0 404 Not Found\r\nContent-Length: %d\r\nContent-Type: text/html\r\n\r\n", info->size);
  ret = send(sock, buf, len, 0);
  if (ret < 0)
  {
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return;
  }
  printf("Sending 404 page: %s\n", info->real_path);
  exp1_send_file(sock, info->real_path);
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

path_3XX exp1_get_path_3XX(const char *path)
{
  if (strcmp(path, "/html/301.html") == 0)
  {
    return CODE_301;
  }
  else if (strcmp(path, "/html/302.html") == 0)
  {
    return CODE_302;
  }
  else if (strcmp(path, "/html/303.html") == 0)
  {
    return CODE_303;
  }
  else if (strcmp(path, "/html/307.html") == 0)
  {
    return CODE_307;
  }
  else if (strcmp(path, "/html/308.html") == 0)
  {
    return CODE_308;
  }
  return -1;
}
