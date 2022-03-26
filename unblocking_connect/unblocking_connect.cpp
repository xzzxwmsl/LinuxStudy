#include<unistd.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/ioctl.h>
#include<fcntl.h>
#include<errno.h>
int setnonblocking(int fd);
int unblock_connect(const char* ip, const int port, const int time);
int main(int argc, char* argv[]) {
    if (argc <= 2) {
        printf("参数个数错误");
        return -1;
    }
    const char* ip = argv[1];
    const int port = atoi(argv[2]);
    int sockfd = unblock_connect(ip, port, 10);
    if (sockfd < 0) {
        return -1;
    }

    /*
    success, 使用sockfd
    */
    close(sockfd);
    return 0;
}

int unblock_connect(const char* ip, const int port, const int time) {
    int ret = 0;
    // 新建ip地址
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // 设置为非阻塞
    int oldopt = setnonblocking(sockfd);
    ret = connect(sockfd, (sockaddr*)&address, sizeof(address));
    if (ret == 0) {
        // 连接成功，则恢复sockfd的属性，并立即返回
        printf("立即连接\n");
        fcntl(sockfd, F_SETFL, oldopt);
        return sockfd;
    }
    else if (errno != EINPROGRESS) {
        // 只有当errno是EINPROGRESS时才表示连接还在进行
        printf("不支持非阻塞connect\n");
        return -1;
    }

    fd_set readfds, writefds;
    FD_ZERO(&readfds);
    // 将socket与写文件描述符集绑定
    FD_SET(sockfd, &writefds);

    timeval timeout;
    timeout.tv_sec = time;
    timeout.tv_usec = 0;

    ret = select(sockfd + 1, NULL, &writefds, NULL, &timeout);
    if (ret <= 0) {
        // select超时或者出错，立即返回
        printf("select timeout\n");
        close(sockfd);
        return -1;
    }
    if (!FD_ISSET(sockfd, &writefds)) {
        // 未就绪
        printf("no events on sockfd found!\n");
        return -1;
    }
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
        printf("get socket option failed\n");
        close(sockfd);
        return -1;
    }
    if (error != 0) {
        printf("error code : %d\n", error);
        return -1;
    }
    // success 
    fcntl(sockfd, F_SETFL, oldopt);
    return sockfd;
}

int setnonblocking(int fd) {
    int oldopt = fcntl(fd, F_GETFL);
    int newopt = oldopt | O_NONBLOCK;
    fcntl(fd, F_SETFL, newopt);
    return oldopt;
}
