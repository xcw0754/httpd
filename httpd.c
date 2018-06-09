#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/*
 * 处理连接
 */
void accept_request(int client)
{
    // Tips:少用魔数
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    // Tips:少用单字符的变量名
    size_t i, j;
    struct stat st;
    // 初写httpd可以了解下cgi的概念
    int cgi = 0;
    char *query_string = NULL;

    // 从client中读取1行
    numchars = get_line(client, buf, sizeof(buf));
    i = 0;
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    // 判别http头部的method，只接受GET和POST
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    // post的数据准备交给cgi处理
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    // 取出http header首行的uri
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    // 去掉uri中的参数
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    // path是资源的路径
    sprintf(path, "htdocs%s", url);
    // 访问根路径则返回index.html页面
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    if (stat(path, &st) == -1) {
        // 资源不存在，读取并忽略余下的header行，并返回一个404页面
        while ((numchars > 0) && strcmp("\n", buf)) 
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        // 资源存在，妥善处理
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");

        if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
            cgi = 1;
        
        if (!cgi)
            serve_file(client, path);   // 返回普通文件
        else
            execute_cgi(client, path, method, query_string); // 交给cgi处理后再返回
    }

    close(client);
}

/*
 * 回复400 bad request
 */
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/*
 * 读取文件resource并发送到client
 */
void cat(int client, FILE *resource)
{
    char buf[1024];

    // 逐行读取，逐行发送
    fgets(buf, sizeof(buf), resource);
    // feof可判断文件流是否读完, eof = end of file
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/*
 * 执行不了cgi时返回500错误页面
 */
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/*
 * 打印错误并退出
 */
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/*
 * 执行cgi
 */
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
    {
        // 忽略余下的头部行
        while ((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));
    }
    else
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';  //sizeof("Content-Length") = 15
            // &(buf[16]) 标记着数据长度，是个字符串的地址
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length <= -1) {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    
    // pipe函数可创建单向管道，cgi_output是两个句柄
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    // 再加一个管道，这样两个单向管道就可以使进程双向通信
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }
    // 用新进程来执行cgi，新旧进程共享当前所有打开的句柄
    // 上面的input和output是对于新进程而言的
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }

    if (pid == 0) // 新进程：执行cgi
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        // dup2(oldfd, newfd) 是先关闭newfd，并将newfd作为oldfd的拷贝
        // 可以理解为原newfd已作废了，现在操作newfd就跟操作oldfd一样
        // 目的:读写标准输入/输出时,实际是在读写管道
        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);

        close(cgi_output[0]);
        close(cgi_input[1]);

        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        // 输出到当前环境变量，方便执行cgi时直接获取(见两个cgi文件)
        putenv(meth_env);

        if (strcasecmp(method, "GET") == 0) 
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else 
        {
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        // 执行cgi(cgi是语言无关的)
        execl(path, path, NULL);
        exit(0);

    } else {    // 旧进程
        // 不用的管道端通常在这个位置关闭
        // cgi_output在旧进程只读不写，所以关闭写端
        close(cgi_output[1]);   
        // cgi_input在旧进程只写不读，所以关闭读端
        close(cgi_input[0]); 

        if (strcasecmp(method, "POST") == 0)
        {
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                // POST上来的数据都写管道里
                write(cgi_input[1], &c, 1);
            }
        }

        // 读管道的数据发回给客户端(数据是新进程即cgi的处理结果)
        // read是阻塞的，返回值非1时可推断出新进程执行完后关闭了管道
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        // 阻塞等待新进程执行完毕完全退出
        waitpid(pid, &status, 0);
    }
}

/*
 * 从sock读取一行数据，最多可以装满buf
 * 注：这个函数是逐个字符读取的，不适用于大并发量 */
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        // 单个字符地读
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)  // 正常读得到一个字符
        {
            
            if (c == '\r')
            {
                // \r\n是区分http头部行之间的标志，这里要忽略掉
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    // 这里没有判断返回值，c未必是预期的值
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else    // 读不到一个字符的原因可能多样的
            c = '\n';
    }
    buf[i] = '\0';
    return i;
}

/*
 * 填充头部
 */
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename; // 意义是？

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");        // 这个\r\n别漏了
    send(client, buf, strlen(buf), 0);
}

/*
 * 返回404页面
 */
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}


/*
 * 发一个普通文件发给client
 */
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/*
 * tcp监听一个口
 */
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;

    // PF_INET 表示协议类型
    // SOCKET_STREAM 表示流式接口，即指代tcp
    // 1) 创建socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    // INADDR_ANY 表示所有可用ip地址，即0.0.0.0
    // 注：0.0.0.0地址可被其他机器感知，而127.0.0.1只能被本机感知
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    // 2) 绑定端口与地址
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  
    {
        int namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        // 这是真正的端口，bind之后name.sin_port会被填充为正在监听的端口
        *port = ntohs(name.sin_port);
    }
    // 3) 开始监听。
    // 参数backlog是已完成3次握手且未被accept的已连接数的上限
    // 而未完成3次握手的连接数的上限在/proc/sys/net/ipv4/tcp_max_syn_backlog
    // 注：如果有并发要求，可以再研究如何提高这些值
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return httpd;
}

/*
 * 返回501 未实现
 */
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}


int main(void)
{
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);

    // 随机监听一个端口(port=0)
    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        // client_name可以拿到客户端的信息
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len);
        if (client_sock == -1)
            error_die("accept");

        accept_request(client_sock);
    }

    close(server_sock);
    return(0);
}
