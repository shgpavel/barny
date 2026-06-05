#include "test_framework.h"
#include "barny.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "../src/ipc/sway_ipc.c"

static int
read_full(int fd, void *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		ssize_t n = read(fd, (char *)buf + off, len - off);
		if (n <= 0) {
			return -1;
		}
		off += (size_t)n;
	}

	return 0;
}

static int
write_full(int fd, const void *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		ssize_t n = write(fd, (const char *)buf + off, len - off);
		if (n <= 0) {
			return -1;
		}
		off += (size_t)n;
	}

	return 0;
}

static void
close_pair(int fds[2])
{
	close(fds[0]);
	close(fds[1]);
}

void
test_ipc_send_framing(void)
{
	TEST_SUITE_BEGIN("sway_ipc_send");

	TEST("writes magic, length, type, and payload")
	{
		barny_state_t state;
		const char   *payload;
		uint32_t      type;
		uint8_t       header[SWAY_IPC_HEADER_SIZE];
		uint32_t      len;
		uint32_t      got_type;
		char          buf[16] = { 0 };
		int           fds[2];

		ASSERT_EQ_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

		memset(&state, 0, sizeof(state));
		state.sway_ipc_fd = fds[0];

		payload           = "hello";
		type              = 42;
		len               = 0;
		got_type          = 0;

		ASSERT_EQ_INT(0, barny_sway_ipc_send(&state, type, payload));

		ASSERT_EQ_INT(0, read_full(fds[1], header, sizeof(header)));

		ASSERT_TRUE(memcmp(header, SWAY_IPC_MAGIC, 6) == 0);

		memcpy(&len, header + 6, 4);
		memcpy(&got_type, header + 10, 4);

		ASSERT_EQ_INT((int)strlen(payload), (int)len);
		ASSERT_EQ_INT((int)type, (int)got_type);

		buf[0] = '\0';
		ASSERT_EQ_INT(0, read_full(fds[1], buf, len));
		ASSERT_EQ_STR(payload, buf);

		close_pair(fds);
	}

	TEST_SUITE_END();
}

void
test_ipc_recv_framing(void)
{
	TEST_SUITE_BEGIN("sway_ipc_recv");

	TEST("reads header and payload")
	{
		barny_state_t state;
		const char   *payload;
		uint32_t      type;
		uint32_t      len;
		uint8_t       header[SWAY_IPC_HEADER_SIZE];
		uint32_t      out_type;
		char         *out;
		int           fds[2];

		ASSERT_EQ_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

		memset(&state, 0, sizeof(state));
		state.sway_ipc_fd = fds[0];

		payload           = "world";
		type              = 7;
		len               = (uint32_t)strlen(payload);
		out_type          = 0;

		memcpy(header, SWAY_IPC_MAGIC, 6);
		memcpy(header + 6, &len, 4);
		memcpy(header + 10, &type, 4);

		ASSERT_EQ_INT(0, write_full(fds[1], header, sizeof(header)));
		ASSERT_EQ_INT(0, write_full(fds[1], payload, len));

		out = barny_sway_ipc_recv(&state, &out_type);
		ASSERT_NOT_NULL(out);
		ASSERT_EQ_INT((int)type, (int)out_type);
		ASSERT_EQ_STR(payload, out);
		free(out);

		close_pair(fds);
	}

	TEST("handles zero-length payload")
	{
		barny_state_t state;
		uint32_t      type;
		uint32_t      len;
		uint8_t       header[SWAY_IPC_HEADER_SIZE];
		uint32_t      out_type;
		char         *out;
		int           fds[2];

		ASSERT_EQ_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

		memset(&state, 0, sizeof(state));
		state.sway_ipc_fd = fds[0];

		type              = 9;
		len               = 0;
		out_type          = 0;

		memcpy(header, SWAY_IPC_MAGIC, 6);
		memcpy(header + 6, &len, 4);
		memcpy(header + 10, &type, 4);

		ASSERT_EQ_INT(0, write_full(fds[1], header, sizeof(header)));

		out = barny_sway_ipc_recv(&state, &out_type);
		ASSERT_NOT_NULL(out);
		ASSERT_EQ_INT((int)type, (int)out_type);
		ASSERT_EQ_STR("", out);
		free(out);

		close_pair(fds);
	}

	TEST("rejects invalid magic")
	{
		barny_state_t state;
		uint8_t       header[SWAY_IPC_HEADER_SIZE];
		uint32_t      out_type;
		char         *out;
		int           fds[2];

		ASSERT_EQ_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

		memset(&state, 0, sizeof(state));
		state.sway_ipc_fd = fds[0];
		out_type          = 0;

		memset(header, 0, sizeof(header));
		memcpy(header, "badmgc", 6);
		ASSERT_EQ_INT(0, write_full(fds[1], header, sizeof(header)));

		out = barny_sway_ipc_recv(&state, &out_type);
		ASSERT_NULL(out);

		close_pair(fds);
	}

	TEST_SUITE_END();
}

TEST_MAIN_BEGIN()

RUN_SUITE(test_ipc_send_framing);
RUN_SUITE(test_ipc_recv_framing);

TEST_MAIN_END()
