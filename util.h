#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#define __unused __attribute__((unused))

#define container_of(base, type, member) \
	((type *)((void *)base - offsetof(type, member)))

static _Noreturn void fatal(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "fatal error: ");
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	exit(EXIT_FAILURE);
}

#endif
