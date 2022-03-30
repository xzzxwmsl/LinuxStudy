#ifndef LST_TIMER
#define LST_TIMER

#include<time.h>
#include<netinet/in.h>
#include<stdio.h>
class util_timer;

const int BUF_SIZE = 64;
struct client_data {
	sockaddr_in addr;
	int sockfd;
	char buf[64];
	util_timer* timer;
};

class util_timer {
public:
	time_t expire; // 任务的超时时间
	void (*cb_func)(client_data*); // callback fucntion
	client_data* user_data;
	util_timer* pre, *next;

	util_timer() : pre(nullptr), next(nullptr) {}
};

class sort_timer_lst {
private:
	// 原类没有头尾节点，不易于编程实现，这里给它加上头尾节点
	util_timer* head, * tail;
	// 计数器
	int cnt = 0;
public:
	sort_timer_lst() {
		head = new util_timer();
		tail = new util_timer();
		head->next = tail;
		tail->pre = head;
	}

	~sort_timer_lst() {
		util_timer* p = head;
		while (p)
		{
			head = p->next;
			delete p;
			p = head;
		}
	}

	void add_timer(util_timer* timer) {
		if (timer == nullptr) {
			return;
		}
		util_timer* p = head;
		while (p->next != tail && timer->expire >= p->next->expire) {
			p = p->next;
		}
		// 插入
		insert_after(p, timer);
		cnt++;
		return;
	}

	// 从pos向后查找插入位置
	void add_timer(util_timer* timer, util_timer *pos) {
		if (timer == nullptr) {
			return;
		}
		util_timer* p = pos;
		while (p->next != tail && timer->expire >= p->next->expire) {
			p = p->next;
		}
		// 插入
		insert_after(p, timer);
		cnt++;
		return;
	}

	void insert_after(util_timer* pre, util_timer* timer) {
		timer->next = pre->next;
		timer->pre = pre;
		pre->next = timer;
		timer->next->pre = timer;
	}

	void del_timer(util_timer* timer) {
		if (timer == nullptr || cnt == 0 || timer == head || timer == tail) {
			return;
		}
		cnt--;
		util_timer* pre = timer->pre, * next = timer->next;
		pre->next = next;
		next->pre = pre;
		delete timer;
	}

	void adjust_timer(util_timer* timer) {
		// 仅考虑定时器超时延长的情况，向后调整它的位置
		if (timer == nullptr) return;
		// 不必调整
		if (timer->next == tail || timer->expire < timer->next->expire) {
			return;
		}
		// 将timer取出来，从pre开始向后寻找插入位置
		util_timer* pre = timer->pre, * next = timer->next;
		pre->next = next;
		next->pre = pre;

		cnt--; // 保持cnt数量统一
		add_timer(timer, pre);
	}

	// SIGALRM 信号每次被触发就在其信号处理函数中执行一次trick函数，以处理链表上的到期任务
	void trick() {
		if (cnt == 0 || head->next == tail) {
			return;
		}
		printf("timer trick\n");
		time_t curtime = time(NULL); // system current time
		util_timer* p = head->next;
		while (p != tail) {
			if (curtime < p->expire) {
				// 还没到定时时间，后面的也不必执行
				break;
			}
			// 执行
			p->cb_func(p->user_data);
			// 从链表下摘下
			util_timer* temp = p->next;
			del_timer(p);
			p = temp;
		}
	}
};

# endif
