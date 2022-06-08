#ifndef POLLER_H
#define POLLER_H

struct poller_event {
	int fd;
};

struct poller {
	int (*poll_add)(struct poller *, int fd);
	int (*poll_delete)(struct poller *, int fd);
	void (*poll)(struct poller *, struct poller_event *, size_t *);
};

struct poller *poller_create(void);
void poller_destroy(struct poller *poller);

#endif
