#include <sys/epoll.h>

#include <stdlib.h>

#include "poller.h"
#include "util.h"

struct epoll_poller {
	int epollfd;
	struct poller poller;
};

static int epoll_operate(struct poller *poller, int fd, int op)
{
	struct epoll_poller *epoller;
	struct epoll_event event;

	epoller = container_of(poller, struct epoll_poller, poller);

	if (op != EPOLL_CTL_DEL) {
		event.events = EPOLLIN | EPOLLET;
		event.data.fd = fd;
	}
	return epoll_ctl(epoller->epollfd, op, fd, &event);
}

static int epoll_poll_add(struct poller *poller, int fd)
{
	return epoll_operate(poller, fd, EPOLL_CTL_ADD);
}

static int epoll_poll_del(struct poller *poller, int fd)
{
	return epoll_operate(poller, fd, EPOLL_CTL_DEL);
}

static void epoll_poll(struct poller *poller, struct poller_event *pevents,
		       size_t *len)
{
	struct epoll_poller *epoller;
	struct epoll_event events[*len];
	int n;

	epoller = container_of(poller, struct epoll_poller, poller);

	if ((n = epoll_wait(epoller->epollfd, events, *len, -1)) < 0) {
		fatal("epoll_wait: %m");
	}

	for (int i = 0, j = 0; i < n; i++) {
		if (events[i].data.fd > 0 && events[i].events & EPOLLIN) {
			pevents[j++].fd = events[i].data.fd;
			*len = j;
		}
	}
}

static struct poller poller_ops = {
	.poll = epoll_poll,
	.poll_add = epoll_poll_add,
	.poll_delete = epoll_poll_del,
};

static void epoll_poller_destroy(struct poller *poller)
{
	struct epoll_poller *epoller;

	epoller = container_of(poller, struct epoll_poller, poller);
	free(epoller);
}

struct poller *poller_create(void)
{
	struct epoll_poller *poller;

	poller = malloc(sizeof(*poller));
	poller->poller = poller_ops;

	if ((poller->epollfd = epoll_create(1)) < 0) {
		fatal("epoll_create1: %m");
	}
	return (struct poller *)&poller->poller;
}

void poller_destroy(struct poller *poller)
{
	epoll_poller_destroy(poller);
}
