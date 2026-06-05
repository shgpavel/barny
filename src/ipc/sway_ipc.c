#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#include "barny.h"

#define SWAY_IPC_MAGIC       "i3-ipc"
#define SWAY_IPC_HEADER_SIZE 14

typedef struct {
	char     magic[6];
	uint32_t length;
	uint32_t type;
} __attribute__((packed)) sway_ipc_header_t;

int
barny_sway_ipc_init(barny_state_t *state)
{
	const char        *socket_path = getenv("SWAYSOCK");
	struct sockaddr_un addr;
	int                flags;

	if (!socket_path) {
		fprintf(stderr, "barny: SWAYSOCK not set, sway IPC unavailable\n");
		state->sway_ipc_fd = -1;
		return -1;
	}

	state->sway_ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (state->sway_ipc_fd < 0) {
		fprintf(stderr, "barny: failed to create socket: %s\n",
		        strerror(errno));
		return -1;
	}

	addr = (struct sockaddr_un){ .sun_family = AF_UNIX };
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

	if (connect(state->sway_ipc_fd, (struct sockaddr *)&addr, sizeof(addr))
	    < 0) {
		fprintf(stderr, "barny: failed to connect to sway: %s\n",
		        strerror(errno));
		close(state->sway_ipc_fd);
		state->sway_ipc_fd = -1;
		return -1;
	}

	flags = fcntl(state->sway_ipc_fd, F_GETFL, 0);
	fcntl(state->sway_ipc_fd, F_SETFL, flags | O_NONBLOCK);

	printf("barny: connected to sway IPC\n");
	return 0;
}

void
barny_sway_ipc_cleanup(barny_state_t *state)
{
	if (state->sway_ipc_fd >= 0) {
		close(state->sway_ipc_fd);
		state->sway_ipc_fd = -1;
	}
}

int
barny_sway_ipc_send(barny_state_t *state, uint32_t type, const char *payload)
{
	uint32_t      len;
	size_t        total;
	char         *buf;
	ssize_t       offset;
	ssize_t       remaining;
	ssize_t       written;
	struct pollfd pfd;

	if (state->sway_ipc_fd < 0) {
		return -1;
	}

	len   = payload ? strlen(payload) : 0;
	total = SWAY_IPC_HEADER_SIZE + len;
	buf   = malloc(total);

	memcpy(buf, SWAY_IPC_MAGIC, 6);
	memcpy(buf + 6, &len, 4);
	memcpy(buf + 10, &type, 4);
	if (payload && len > 0) {
		memcpy(buf + 14, payload, len);
	}

	offset    = 0;
	remaining = total;
	while (remaining > 0) {
		written = write(state->sway_ipc_fd, buf + offset, remaining);
		if (written < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				pfd = (struct pollfd){ .fd     = state->sway_ipc_fd,
					               .events = POLLOUT };
				poll(&pfd, 1, 100);
				continue;
			}
			fprintf(stderr, "barny: failed to write to sway IPC: %s\n",
			        strerror(errno));
			free(buf);
			return -1;
		}
		offset    += written;
		remaining -= written;
	}
	free(buf);

	return 0;
}

char *
barny_sway_ipc_recv(barny_state_t *state, uint32_t *type)
{
	sway_ipc_header_t header;
	ssize_t           hdr_offset;
	char             *payload;
	ssize_t           remaining;
	ssize_t           offset;
	ssize_t           n;
	struct pollfd     pfd;

	if (state->sway_ipc_fd < 0) {
		return NULL;
	}

	hdr_offset = 0;
	while (hdr_offset < (ssize_t)sizeof(header)) {
		n = read(state->sway_ipc_fd,
		         (char *)&header + hdr_offset,
		         sizeof(header) - hdr_offset);
		if (n <= 0) {
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				if (hdr_offset == 0) {
					return NULL;
				}
				pfd = (struct pollfd){ .fd     = state->sway_ipc_fd,
					               .events = POLLIN };
				poll(&pfd, 1, 100);
				continue;
			}
			if (n < 0) {
				fprintf(stderr, "barny: sway IPC read error: %s\n",
				        strerror(errno));
			}
			return NULL;
		}
		hdr_offset += n;
	}

	if (memcmp(header.magic, SWAY_IPC_MAGIC, 6) != 0) {
		fprintf(stderr, "barny: invalid IPC magic\n");
		return NULL;
	}

	*type = header.type;

	if (header.length == 0) {
		return strdup("");
	}

	payload   = malloc(header.length + 1);
	remaining = header.length;
	offset    = 0;

	while (remaining > 0) {
		n = read(state->sway_ipc_fd, payload + offset, remaining);
		if (n <= 0) {
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
				pfd = (struct pollfd){ .fd     = state->sway_ipc_fd,
					               .events = POLLIN };
				poll(&pfd, 1, 100);
				continue;
			}
			free(payload);
			return NULL;
		}
		offset    += n;
		remaining -= n;
	}

	payload[header.length] = '\0';
	return payload;
}

char *
barny_sway_ipc_recv_sync(barny_state_t *state, uint32_t *type, int timeout_ms)
{
	struct pollfd pfd;
	int           ret;

	if (state->sway_ipc_fd < 0) {
		return NULL;
	}

	pfd = (struct pollfd){ .fd = state->sway_ipc_fd, .events = POLLIN };
	ret = poll(&pfd, 1, timeout_ms);
	if (ret <= 0) {
		return NULL;
	}

	return barny_sway_ipc_recv(state, type);
}

int
barny_sway_ipc_subscribe(barny_state_t *state, const char *events)
{
	return barny_sway_ipc_send(state, 2, events);
}
