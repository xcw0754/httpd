#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define HTTP_GET_HEADER "GET / HTTP/1.1\r\nConnection:keep-alive\r\nHost:127.0.0.1\r\n\r\n"

int main(int argc, char *argv[])
{
    int sockfd;
    int len;
    struct sockaddr_in address;
    int result, ret;
    char ch = 'A';
    char buf[1024] = {0};
    u_short port = 0;

    if (argc == 1) {
        printf("please input port number\n");
        return ;
    }

    port = atoi(argv[1]);

    // 步骤1:创建socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(port);
    len = sizeof(address);

    // 步骤2:连接服务器
    result = connect(sockfd, (struct sockaddr *)&address, len);

    if (result == -1)
    {
        perror("oops: client1");
        exit(1);
    }

    // 步骤3:写数据
    snprintf(buf, sizeof(buf), "%s", HTTP_GET_HEADER);
    write(sockfd, buf, strlen(buf));

    // 步骤4:读取返回的数据
    while ((ret = read(sockfd, &ch, 1)) > 0) {
        printf("%c", ch);
    }
    printf("\n");
    close(sockfd);
    sockfd = -1;
    exit(0);
}
