/*
应用层的keepalive机制。使用排序定时器机制。
使用epoll监听fd，监听是否有新的连接建立请求，使用管道传输信号，epoll监听pipefd[0]来检测是否有新的待处理信号。
然后检测客户端fd是否有新的可读数据，如果有的话就继续读，然后重置定时器时间。
最后使用超时标志位检测是否有超时的connect，如果有的话，就触发定时器函数，将其从epoll上删除并关闭socket。
*/

#include"list_time.h"
#include<netinet/in.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<stdlib.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<assert.h>
#include<fcntl.h>
#include<signal.h>
#include<string.h>
#include<errno.h>
const int FD_LIMIT = 65535;
const int MAX_EVENT_NUM = 1024;
const int TIMESLOT = 5;

static int pipefd[2];
static int epollfd = 0;
static sort_timer_lst timer_lst;

void addfd(int epollfd, int fd);
int setnonblocking(int fd);
void addsig(int sig);
void sig_handler(int sig);
void timer_handler();
void cb_func(client_data* user_data);

int main(int argv, char **argc) {
	if (argv <= 2) {
		printf("Input ip ans port\n");
		return -1;
	}
	const char* ip = argc[1];
	const int port = atoi(argc[2]);

	sockaddr_in addr;
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &addr.sin_addr);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(sockfd >= 0);

	int ret = bind(sockfd, (sockaddr*)&addr, sizeof(addr));
	assert(ret != -1);

	ret = listen(sockfd, 5);
	assert(ret != -1);

	epoll_event events[MAX_EVENT_NUM];
	int epollfd = epoll_create(5);
	assert(epollfd != -1);

	addfd(epollfd, sockfd);

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
	assert(ret != -1);
	setnonblocking(pipefd[1]);
	addfd(epollfd, pipefd[0]);

	// 设置信号处理函数
	addsig(SIGALRM);
	addsig(SIGTERM);
	bool server_stop = false;

	client_data* users = new client_data[FD_LIMIT];
	bool timeout = false;
	alarm(TIMESLOT);

	while (server_stop == false) {
		int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
		if (num < 0 && errno != EINTR) {
			printf("epoll_wait failure\n");
			break;
		}
		for (int i = 0; i < num; i++) {
			int fd = events[i].data.fd;
			if (fd == sockfd) {
				// 有新的客户连接进入
				sockaddr_in client_addr;
				socklen_t len = sizeof(client_addr);
				int connfd = accept(fd, (sockaddr*)&client_addr, &len);
				addfd(epollfd, connfd);
				users[connfd].addr = client_addr;
				users[connfd].sockfd = connfd;
				// 创建定时器
				util_timer* timer = new util_timer();
				//timer->cb_func = cb_func;
				timer->expire = time(NULL) + 3 * TIMESLOT;
				timer->user_data = &users[connfd];

				users[connfd].timer = timer;
				timer_lst.add_timer(timer);
			}
			else if (fd == pipefd[0] && events[i].events & EPOLLIN) {
				// 处理信号
				int sig;
				char signals[1024];
				ret = recv(pipefd[0], signals, sizeof(signals), 0);
				if (ret == -1) {
					// handle the error
					continue;
				}
				else if (ret == 0) {
					continue;
				}
				else {
					for (int j = 0; j < ret; j++) {
						switch (signals[j]) {
						case SIGALRM:
							// 用timeout标记有定时任务需要处理，但不立即处理；因为定时任务的优先级并非最高
							timeout = true;
							break;
						case SIGTERM:
							server_stop = true;
						}
					}
				}
			}
			else if (events[i].events & EPOLLIN) {
				// handler client data
				memset(users[fd].buf, '\0', BUF_SIZE);
				ret = recv(fd, users[fd].buf, BUF_SIZE - 1, 0);
				printf("Get %d len data : %s from %d", ret, users[fd].buf, fd);
				util_timer* timer = users[fd].timer;
				if (ret < 0) {
					// error, close connect and remove timer
					if (errno != EAGAIN) {
						cb_func(&users[fd]);
						if (timer) {
							timer_lst.del_timer(timer);
						}
					}
				}
				else if (ret == 0) {
					// 如果对面已经关闭连接，则处理
					cb_func(&users[fd]);
					if (timer) {
						timer_lst.del_timer(timer);
					}
				}
				else {
					// 客户链接上有数据可读，则需要调整定时器，以延迟关闭时间
					if (timer) {
						timer->expire = time(NULL) + 3 * TIMESLOT;
						printf("Adjust the timer\n");
						timer_lst.adjust_timer(timer);
					}
				}
			}
			else {
				// others
			}
		}

		// 处理完epoll后
		if (timeout) {
			timer_handler();
			timeout = false;
		}
	}
}

void addfd(int epollfd, int fd) {
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

int setnonblocking(int fd) {
	int oldopt = fcntl(fd, F_GETFL);
	int newopt = oldopt | O_NONBLOCK;
	fcntl(fd, F_SETFL, newopt);
	return oldopt;
}

void addsig(int sig) {
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_flags |= SA_RESTART;
	sa.sa_handler = sig_handler;
	sigfillset(&sa.sa_mask);

	assert(sigaction(sig, &sa, NULL) != -1);
}

void sig_handler(int sig) {
	int save_erro = errno;
	int msg = sig;
	send(pipefd[1], (char*)&msg, 1, 0);
	errno = save_erro;
}

void timer_handler() {
	// 定时处理任务
	timer_lst.trick();
	// 因为每次alarm调用只会引起一次SIGALRM信号，所以我们需要重新定时
	alarm(TIMESLOT);
}

// 定时器回调函数，删除epoll上的socket注册事件，并关闭之
void cb_func(client_data* user_data) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
	assert(user_data);
	close(user_data->sockfd);
	printf("关闭fd %d\n", user_data->sockfd);
}
