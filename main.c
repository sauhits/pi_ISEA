#include "exp1.h"
#include "exp1lib.h"
#include <sys/stat.h>
#include <string.h> // strcpy, strlen, strcmp, strstr を使用するために追加
#include <stdio.h>  // sprintf を使用するために追加

typedef struct
{
    char cmd[64];
    char path[256];
    char real_path[256];
    char type[64];
    int code;
    int size;
    char location[256]; // リダイレクト先のURLを格納
} exp1_info_type;

// プロトタイプ宣言
int exp1_http_session(int sock);
int exp1_parse_header(char *buf, int size, exp1_info_type *info);
void exp1_parse_status(char *status, exp1_info_type *pinfo);
void exp1_check_file(exp1_info_type *info);
void exp1_http_reply(int sock, exp1_info_type *info);
void exp1_send_301_page_from_template(int sock, const char *location_url, const char *template_filepath);
void exp1_send_error_page(int sock, int status_code, const char *status_message, const char *html_filepath);
void exp1_send_404(int sock);
void exp1_send_file(int sock, char *filename);
void exp1_send_dynamic_content(int sock, const char *content, size_t content_length, const char *content_type); // 動的コンテンツ送信用ヘルパー

int main(int argc, char **argv)
{
    int sock_listen;

    sock_listen = exp1_tcp_listen(10018);
    printf("[*] Listening on port 10018...\n");
    while (1)
    {
        struct sockaddr addr;
        int sock_client;
        socklen_t len = sizeof(addr); // accept用にlenを初期化

        sock_client = accept(sock_listen, &addr, &len);
        if (sock_client < 0)
        {
            perror("accept");
            continue;
        }

        exp1_http_session(sock_client);

        shutdown(sock_client, SHUT_RDWR);
        close(sock_client);
    }
    close(sock_listen);
    return 0; // main関数の戻り値
}

int exp1_http_session(int sock)
{
    char buf[2048];
    int recv_size = 0;
    exp1_info_type info;
    memset(&info, 0, sizeof(exp1_info_type)); // info構造体を初期化
    int ret = 0;

    printf("[*] Waiting to receive request from client...\n");

    // ヘッダの最初の行を受信するまでループ (簡易的な実装)
    while (ret == 0)
    {
        if ((sizeof(buf) - recv_size - 1) <= 0)
        {
            fprintf(stderr, "[!] Buffer overflow risk in recv loop.\n");
            return -1;
        }
        int size = recv(sock, buf + recv_size, (sizeof(buf) - recv_size - 1), 0);

        if (size == -1)
        {
            perror("recv");
            return -1;
        }
        if (size == 0)
        { // クライアントが接続を閉じた
            printf("[*] Client closed connection.\n");
            return -1;
        }

        if (recv_size == 0) // 最初の受信
        {
            printf("[*] Start receiving data from client (first %d bytes)\n", size);
        }

        recv_size += size;
        buf[recv_size] = '\0'; // 受信データを文字列として扱えるようにする

        if (strstr(buf, "\r\n") != NULL)
        {
            ret = exp1_parse_header(buf, recv_size, &info);
        }
    }

    exp1_http_reply(sock, &info);

    return 0;
}

int exp1_parse_header(char *buf, int size, exp1_info_type *info)
{
    // printf("[*] Parsing HTTP header...\n");
    char status_line[1024];
    int i, j;

    char *newline_pos = strstr(buf, "\r\n");
    if (newline_pos == NULL)
    {
        return 0; // まだステータスラインが完全に受信できていない
    }

    int status_line_len = newline_pos - buf;
    if (status_line_len >= sizeof(status_line))
    {
        fprintf(stderr, "[!] Status line too long.\n");
        info->code = 400; // Bad Request
        return 1;         // パース完了（エラーとして）
    }

    strncpy(status_line, buf, status_line_len);
    status_line[status_line_len] = '\0';

    exp1_parse_status(status_line, info); // ステータスラインをパース

    // リダイレクトでなければファイルをチェック
    if (info->code != 301)
    {
        exp1_check_file(info);
    }

    printf("[*] Header parsing completed. Request Path: %s, Code: %d\n", info->path, info->code);
    return 1; // パース完了
}

void exp1_parse_status(char *status_line, exp1_info_type *pinfo)
{
    // printf("[*] Parsing status line: %s\n", status_line); // ユーザー提示コードではコメントアウト
    char cmd[64];   // info構造体のサイズに合わせる
    char path[256]; // info構造体のサイズに合わせる
    int i, j;

    enum state_type
    {
        SEARCH_CMD,
        SEARCH_PATH,
        SEARCH_END
    } state;

    state = SEARCH_CMD;
    j = 0;
    // status_line の終端までループ
    for (i = 0; status_line[i] != '\0' && i < 1024 - 1; i++) // バッファサイズも考慮
    {
        switch (state)
        {
        case SEARCH_CMD:
            if (status_line[i] == ' ')
            {
                if (j < sizeof(cmd))
                { // バッファオーバーフロー防止
                    cmd[j] = '\0';
                    j = 0;
                    state = SEARCH_PATH;
                }
                else
                {
                    fprintf(stderr, "[!] Command too long in status line.\n");
                    strcpy(pinfo->cmd, "INVALID"); // エラーを示す
                    pinfo->code = 400;             // Bad Request
                    return;
                }
            }
            else
            {
                if (j < sizeof(cmd) - 1)
                { // NULL終端文字の分を確保
                    cmd[j] = status_line[i];
                    j++;
                }
                else
                {
                    fprintf(stderr, "[!] Command too long in status line.\n");
                    strcpy(pinfo->cmd, "INVALID");
                    pinfo->code = 400;
                    return;
                }
            }
            break;
        case SEARCH_PATH:
            if (status_line[i] == ' ')
            {
                if (j < sizeof(path))
                { // バッファオーバーフロー防止
                    path[j] = '\0';
                    j = 0;
                    state = SEARCH_END; // HTTPバージョン部分は無視するのでここで終了
                }
                else
                {
                    fprintf(stderr, "[!] Path too long in status line.\n");
                    strcpy(pinfo->path, "/INVALID"); // エラーを示す
                    pinfo->code = 400;               // Bad Request
                    return;
                }
            }
            else
            {
                if (j < sizeof(path) - 1)
                { // NULL終端文字の分を確保
                    path[j] = status_line[i];
                    j++;
                }
                else
                {
                    fprintf(stderr, "[!] Path too long in status line.\n");
                    strcpy(pinfo->path, "/INVALID");
                    pinfo->code = 400;
                    return;
                }
            }
            break;
        case SEARCH_END:
            // HTTPバージョンなどの解析は省略
            goto end_parse_loop; // ループを抜ける
        }
    }
end_parse_loop:

    // ループ終了後、stateがSEARCH_PATHのままなら、パスの終端処理
    if (state == SEARCH_PATH)
    {
        if (j < sizeof(path))
        {
            path[j] = '\0';
        }
        else
        {
            fprintf(stderr, "[!] Path too long (no trailing space) in status line.\n");
            strcpy(pinfo->path, "/INVALID");
            pinfo->code = 400;
            return;
        }
    }

    // cmd と path が空でないことを確認
    if (strlen(cmd) == 0 || strlen(path) == 0)
    {
        // 簡易的なチェック。より厳密な検証が必要な場合もある。
        fprintf(stderr, "[!] Failed to parse command or path from status line: %s\n", status_line);
        if (strlen(cmd) == 0)
            strcpy(pinfo->cmd, "INVALID");
        if (strlen(path) == 0)
            strcpy(pinfo->path, "/INVALID");
        pinfo->code = 400; // Bad Request
        return;
    }

    strncpy(pinfo->cmd, cmd, sizeof(pinfo->cmd) - 1);
    pinfo->cmd[sizeof(pinfo->cmd) - 1] = '\0';
    strncpy(pinfo->path, path, sizeof(pinfo->path) - 1);
    pinfo->path[sizeof(pinfo->path) - 1] = '\0';

    // --- 301リダイレクト設定 ---
    // 例: "/old.html" へのアクセスを "/new.html" へリダイレクト
    if (strcmp(pinfo->path, "/old.html") == 0)
    {
        pinfo->code = 301;
        // リダイレクト先のパスを設定 (絶対パスを推奨)
        strncpy(pinfo->location, "/new.html", sizeof(pinfo->location) - 1);
        pinfo->location[sizeof(pinfo->location) - 1] = '\0'; // NULL終端
        printf("[*] Redirect rule matched: %s -> %s\n", pinfo->path, pinfo->location);
    }
    else if (strcmp(pinfo->path, "/legacy/page") == 0)
    {
        pinfo->code = 301;
        strncpy(pinfo->location, "/archive/new_page.php", sizeof(pinfo->location) - 1);
        pinfo->location[sizeof(pinfo->location) - 1] = '\0';
        printf("[*] Redirect rule matched: %s -> %s\n", pinfo->path, pinfo->location);
    }
    // 他のリダイレクトルールもここに追加可能
}

void exp1_check_file(exp1_info_type *info)
{
    // 既にリダイレクトやエラーコードが設定されている場合はファイルチェックを行わない
    if (info->code == 301 || info->code == 400)
    { // 400 Bad Requestなどもここで弾く
        return;
    }

    struct stat s;
    int ret;
    char *pext;

    // 公開ディレクトリの指定（バッファオーバーフローに注意）
    snprintf(info->real_path, sizeof(info->real_path), "/home/pi/SU-html%s", info->path);

    // パストラバーサル対策の簡易チェック
    if (strstr(info->real_path, "..") != NULL)
    {
        info->code = 400; // Bad Request (または403 Forbidden)
        printf("[!] Path traversal attempt: %s\n", info->path);
        return;
    }

    ret = stat(info->real_path, &s);

    if (ret == 0 && S_ISDIR(s.st_mode))
    { // ディレクトリの場合
        // index.html を探す (バッファオーバーフローに注意)
        char temp_path[sizeof(info->real_path)];
        strncpy(temp_path, info->real_path, sizeof(temp_path) - 1);
        temp_path[sizeof(temp_path) - 1] = '\0';

        // 末尾にスラッシュがなければ追加
        if (temp_path[strlen(temp_path) - 1] != '/')
        {
            strncat(temp_path, "/", sizeof(temp_path) - strlen(temp_path) - 1);
        }
        strncat(temp_path, "index.html", sizeof(temp_path) - strlen(temp_path) - 1);
        strncpy(info->real_path, temp_path, sizeof(info->real_path) - 1);
        info->real_path[sizeof(info->real_path) - 1] = '\0';

        ret = stat(info->real_path, &s); // index.htmlの存在を再チェック
    }

    if (ret == -1)
    { // ファイル/ディレクトリが存在しない or アクセス権なし
        info->code = 404;
        printf("[*] File not found or permission issue for: %s (resolved: %s)\n", info->path, info->real_path);
    }
    else if (!S_ISREG(s.st_mode))
    {                     // 通常のファイルではない場合 (ディレクトリでindex.htmlもなかった場合など)
        info->code = 403; // Forbidden
        printf("[*] Not a regular file or access forbidden: %s (resolved: %s)\n", info->path, info->real_path);
    }
    else
    { // 通常のファイルが見つかった場合
        info->code = 200;
        info->size = (int)s.st_size;

        // Content-Typeの判定 (strrchrで最後の'.'から拡張子を取得するのが一般的)
        pext = strrchr(info->real_path, '.'); // 最後のドット以降を取得
        if (pext != NULL)
        {
            if (strcmp(pext, ".html") == 0 || strcmp(pext, ".htm") == 0)
            {
                strcpy(info->type, "text/html; charset=UTF-8");
            }
            else if (strcmp(pext, ".jpg") == 0 || strcmp(pext, ".jpeg") == 0)
            {
                strcpy(info->type, "image/jpeg");
            }
            else if (strcmp(pext, ".png") == 0)
            {
                strcpy(info->type, "image/png");
            }
            else if (strcmp(pext, ".css") == 0)
            {
                strcpy(info->type, "text/css");
            }
            else if (strcmp(pext, ".js") == 0)
            {
                strcpy(info->type, "application/javascript");
            }
            else
            {
                strcpy(info->type, "application/octet-stream"); // 不明な場合は汎用バイナリ
            }
        }
        else
        {
            strcpy(info->type, "application/octet-stream"); // 拡張子なし
        }
        printf("[*] File checked: %s, Size: %d, Type: %s\n", info->real_path, info->size, info->type);
    }
}

void exp1_http_reply(int sock, exp1_info_type *info)
{
    char buf[1024]; // ヘッダ送信用のバッファ (ファイル本体は別)
    int len;
    int ret;

    // GET以外のメソッドは501 Not Implementedなどを返すのが適切
    if (strcmp(info->cmd, "GET") != 0)
    {
        len = snprintf(buf, sizeof(buf),
                       "HTTP/1.0 501 Not Implemented\r\n"
                       "Content-Type: text/plain\r\n"
                       "Connection: close\r\n\r\n"
                       "Method %s not implemented.",
                       info->cmd);
        send(sock, buf, len, 0);
        printf("[!] Unsupported method: %s\n", info->cmd);
        return;
    }

    switch (info->code)
    {
    case 200:
        len = snprintf(buf, sizeof(buf), "HTTP/1.0 200 OK\r\n");
        len += snprintf(buf + len, sizeof(buf) - len, "Content-Length: %d\r\n", info->size);
        len += snprintf(buf + len, sizeof(buf) - len, "Content-Type: %s\r\n", info->type);
        len += snprintf(buf + len, sizeof(buf) - len, "Connection: close\r\n"); // HTTP/1.0なので
        len += snprintf(buf + len, sizeof(buf) - len, "\r\n");

        ret = send(sock, buf, len, 0);
        if (ret < 0)
        {
            perror("send 200 header");
            return; // socket closeは呼び出し元で行う
        }
        if (info->size > 0)
        { // サイズ0のファイルはボディ送信不要
            exp1_send_file(sock, info->real_path);
        }
        break; // case 200 の終わり

    case 301:
        exp1_send_301(sock, info->location); // locationを渡す
        printf("[*] Sent 301 Redirect for %s to %s\n", info->path, info->location);
        break; // case 301 の終わり

    case 400: // Bad Request (パースエラーなど)
        len = snprintf(buf, sizeof(buf),
                       "HTTP/1.0 400 Bad Request\r\n"
                       "Content-Type: text/html; charset=UTF-8\r\n"
                       "Connection: close\r\n\r\n"
                       "<html><body><h1>400 Bad Request</h1></body></html>");
        send(sock, buf, len, 0);
        printf("[*] Sent 400 Bad Request for path: %s\n", info->path);
        break;

    case 403: // Forbidden
        len = snprintf(buf, sizeof(buf),
                       "HTTP/1.0 403 Forbidden\r\n"
                       "Content-Type: text/html; charset=UTF-8\r\n"
                       "Connection: close\r\n\r\n"
                       "<html><body><h1>403 Forbidden</h1></body></html>");
        send(sock, buf, len, 0);
        printf("[*] Sent 403 Forbidden for path: %s, real_path: %s\n", info->path, info->real_path);
        break;

    case 404:
        exp1_send_404(sock); // 既存の関数を利用
        printf("[*] Sent 404 Not Found for %s (resolved: %s)\n", info->path, info->real_path);
        break; // case 404 の終わり

    default:
        // その他の予期せぬコードの場合は500 Internal Server Errorを返すなど
        len = snprintf(buf, sizeof(buf),
                       "HTTP/1.0 500 Internal Server Error\r\n"
                       "Content-Type: text/plain\r\n"
                       "Connection: close\r\n\r\n"
                       "Unexpected server status code: %d",
                       info->code);
        send(sock, buf, len, 0);
        printf("[!] Undefined Code or unhandled case: %d for path %s\n", info->code, info->path);
        break; // default の終わり
    }
}

// 301レスポンス送信関数
void exp1_send_301(int sock, const char *location_path)
{
    char buf[1024]; // ヘッダ用バッファ
    int len;

    len = snprintf(buf, sizeof(buf), "HTTP/1.0 301 Moved Permanently\r\n");
    len += snprintf(buf + len, sizeof(buf) - len, "Location: %s\r\n", location_path);
    len += snprintf(buf + len, sizeof(buf) - len, "Content-Length: 0\r\n"); // ボディはないので0
    len += snprintf(buf + len, sizeof(buf) - len, "Connection: close\r\n"); // 接続を閉じる
    len += snprintf(buf + len, sizeof(buf) - len, "\r\n");                  // ヘッダの終わり

    if (send(sock, buf, len, 0) < 0)
    {
        perror("send 301 header");
    }
}

void exp1_send_404(int sock)
{
    char buf[1024]; // HTMLメッセージを含むため少し大きめに
    int ret;
    const char *html_body = "<html><head><title>404 Not Found</title></head>"
                            "<body><h1>404 Not Found</h1>"
                            "<p>The requested URL was not found on this server.</p>"
                            "</body></html>";
    int body_len = strlen(html_body);

    // Content-Type と Content-Length を指定する
    int len = snprintf(buf, sizeof(buf),
                       "HTTP/1.0 404 Not Found\r\n"
                       "Content-Type: text/html; charset=UTF-8\r\n"
                       "Content-Length: %d\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "%s",
                       body_len, html_body);

    // printf("%s", buf); // ユーザー提示コードではあったが出力は任意
    ret = send(sock, buf, len, 0); // len は snprintf の戻り値（書き込んだバイト数）

    if (ret < 0)
    {
        perror("send 404 response");
        // shutdown/closeは呼び出し元で行うため、ここでは不要
    }
}

void exp1_send_file(int sock, char *filename)
{
    printf("[*] Sending file: %s\n", filename);
    FILE *fp;
    int len_read;        // freadの戻り値 (読み込んだアイテム数)
    char file_buf[1460]; // TCP MSSを考慮した一般的なバッファサイズ

    fp = fopen(filename, "rb"); // バイナリモード("rb")で開く
    if (fp == NULL)
    {
        perror("fopen in exp1_send_file");
        // ここでエラーをクライアントに通知するのは難しい（既に200 OKヘッダ送信済みのため）
        // ログ出力に留めるか、接続を閉じる程度
        return;
    }

    while ((len_read = fread(file_buf, 1, sizeof(file_buf), fp)) > 0)
    {
        int ret = send(sock, file_buf, len_read, 0);
        if (ret < 0)
        {
            perror("send file content");
            fclose(fp); // エラー時もファイルを閉じる
            return;
        }
    }

    if (ferror(fp))
    { // freadでエラーが発生した場合
        perror("fread in exp1_send_file");
    }

    fclose(fp);
    printf("[*] Finished sending file: %s\n", filename);
}