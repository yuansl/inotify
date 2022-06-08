#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/stat.h>

#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#include "util.h"
#include "poller.h"

#define NUM_EVENTS 1024

static uint32_t default_events = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE |
				 IN_MOVE_SELF | IN_DELETE_SELF;

struct inotify {
	int watchfd;
	const char *path;
};

struct inotify_context {
	int inofd;
	struct inotify *inotifies;
};

static char *inspect_inotify_event(struct inotify_event *event)
{
	static char desc[NAME_MAX];
	int events = event->mask;

#define CONCAT_INOTIFY_EVENT(evname)       \
	({                                 \
		if (strlen(desc) > 0)      \
			strcat(desc, ","); \
		strcat(desc, evname);      \
	})

	desc[0] = '\0';
	if (events & IN_CREATE)
		CONCAT_INOTIFY_EVENT("CREATE");
	if (events & IN_DELETE)
		CONCAT_INOTIFY_EVENT("DELETE");
	if (events & IN_MODIFY)
		CONCAT_INOTIFY_EVENT("MODIFY");
	if (events & IN_ISDIR)
		CONCAT_INOTIFY_EVENT("ISDIR");
	if (events & IN_MOVE_SELF) {
		CONCAT_INOTIFY_EVENT("MOVE_SELF");
	}
	if (events & IN_MOVED_TO)
		CONCAT_INOTIFY_EVENT("MOVED_TO");

	if (events & IN_MOVED_FROM)
		CONCAT_INOTIFY_EVENT("MOVED_FROM");

	if (events & IN_MOVE) {
		printf("IN_MOVE for event: wd=%d, name='%s'\n", event->wd,
		       event->name);

		snprintf(desc + strlen(desc), sizeof(desc) - strlen(desc),
			 " cookie=%d", event->cookie);
	}

	return desc;
}

__nonnull((3)) static char *path_of(const struct inotify *inotify,
				    const struct inotify_event *event,
				    char *path)
{
	if (strstr(inotify->path, event->name) != NULL) {
		return strcpy(path, inotify->path);
	}
	path[0] = '\0';
	strcpy(path, inotify->path);
	if (inotify->path[strlen(path) - 1] != '/') {
		strcat(path, "/");
	}

	return strcat(path, event->name);
}

static int indexof(struct inotify *inotifies, size_t len, int watchfd)
{
	for (int i = 0; i < (int)len; i++) {
		if (inotifies[i].watchfd == watchfd) {
			return i;
		}
	}
	return -1;
}

static void read_inotify_events(int inofd, struct inotify *inotifies,
				size_t len)
{
	struct inotify_event *event;
	static char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
	char path[PATH_MAX];

	event = (struct inotify_event *)buf;

	do {
		struct inotify *inotify = NULL;

		if (read(inofd, buf, sizeof(buf)) < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
				break;
			}
			fatal("read(%d): %m", inofd);
		}

		int idx = indexof(inotifies, len, event->wd);
		if (idx == -1) {
			continue;
		}
		inotify = &inotifies[idx];

		printf("`%s`: %s(event.name=`%s`)\n",
		       path_of(inotify, event, path),
		       inspect_inotify_event(event), event->name);

		if (event->mask & IN_CREATE) {
			path_of(inotify, event, path);
			inotify_add_watch(inofd, path, default_events);
		}
	} while (1);
}

static void inotify_context_destroy(struct inotify_context *ctx)
{
	free(ctx->inotifies);
}

struct inotify_context register_inotify_files(char *files[], size_t len)
{
	int inofd;
	struct inotify_context ctx;
	struct inotify *inotifies;

	if ((inofd = inotify_init1(IN_NONBLOCK)) < 0) {
		fatal("inotify_init1: %m");
	}

	inotifies = calloc(len, sizeof(*inotifies));

	for (int i = 0; i < (int)len && files[i] != NULL; i++) {
		int watchfd;

		watchfd = inotify_add_watch(inofd, files[i], default_events);
		if (watchfd < 0) {
			fprintf(stderr, "inotify_add_watch('%s') error: %m\n",
				files[i]);
			continue;
		}
		inotifies[i] = (struct inotify){
			.watchfd = watchfd,
			.path = files[i],
		};
	}
	ctx.inofd = inofd;
	ctx.inotifies = inotifies;

	return ctx;
}

static void inotify_all_file(int argc, char *argv[])
{
	struct poller *poller;
	struct inotify_context inotityctx;
	size_t len;

	len = argc - 1;
	inotityctx = register_inotify_files(argv + 1, len);

	printf("inotify fd: %d\n", inotityctx.inofd);
	for (int i = 0; i < (int)len; i++) {
		if (inotityctx.inotifies[i].path == NULL) {
			continue;
		}
		printf("watchfd: %d for path: `%s`\n",
		       inotityctx.inotifies[i].watchfd,
		       inotityctx.inotifies[i].path);
	}

	poller = poller_create();
	poller->poll_add(poller, inotityctx.inofd);

	for (;;) {
		struct poller_event events[NUM_EVENTS] = { 0 };
		size_t nready = NUM_EVENTS;

		poller->poll(poller, events, &nready);

		for (int i = 0; i < (int)nready; i++) {
			read_inotify_events(events[i].fd, inotityctx.inotifies,
					    len);
		}
	}

	poller_destroy(poller);

	inotify_context_destroy(&inotityctx);
}

static __unused void inotify_per_file(int argc, char *argv[])
{
	int epollfd;

	epollfd = epoll_create(1);
	for (int i = 1; i < argc; i++) {
		struct epoll_event event;
		int inofd = inotify_init1(IN_NONBLOCK);

		if ((inotify_add_watch(inofd, argv[i], default_events)) < 0) {
			fatal("inotify_add_watch(`%s`): %m\n", argv[i]);
			continue;
		}

		event.data.fd = inofd;
		event.events = EPOLLIN;
		epoll_ctl(epollfd, EPOLL_CTL_ADD, inofd, &event);
	}

	for (;;) {
		struct epoll_event events[NUM_EVENTS];
		int nready;

		if ((nready = epoll_wait(epollfd, events, NUM_EVENTS, -1)) <
		    0) {
			fatal("epoll_wait: %m");
		}
		for (int i = 0; i < nready; i++) {
			struct inotify_event *ievent;
			char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
			ievent = (struct inotify_event *)buf;

			printf("fd %d is ready for read\n", events[i].data.fd);

			if (read(events[i].data.fd, buf, sizeof(buf)) < 0) {
				fatal("read: %m");
			}

			printf("`%s`: %03o\n", ievent->name, ievent->mask);
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s file1 [file2 file3...]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	inotify_all_file(argc, argv);

	return 0;
}
