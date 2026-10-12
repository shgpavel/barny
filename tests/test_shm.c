#define _GNU_SOURCE
#include "test_framework.h"
#include "util.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

void
test_shm_create(void)
{
	TEST_SUITE_BEGIN("Shared Memory Buffers");

	TEST("creates a file of the requested size")
	{
		struct stat st;
		int         fd = barny_shm_create(4096 * 3);

		ASSERT_TRUE(fd >= 0);
		ASSERT_EQ_INT(0, fstat(fd, &st));
		ASSERT_EQ_INT(4096 * 3, (int)st.st_size);
		close(fd);
	}

	TEST("file is close-on-exec")
	{
		int fd = barny_shm_create(64);

		ASSERT_TRUE(fd >= 0);
		ASSERT_TRUE(fcntl(fd, F_GETFD) & FD_CLOEXEC);
		close(fd);
	}

	TEST("mapping is writable and zero-filled")
	{
		size_t         size = 640 * 4 * 32;
		int            fd   = barny_shm_create(size);
		unsigned char *p;
		size_t         i;
		int            zero = 1;

		ASSERT_TRUE(fd >= 0);
		p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		ASSERT_TRUE(p != MAP_FAILED);
		for (i = 0; i < size; i++)
			zero &= p[i] == 0;
		ASSERT_TRUE(zero);
		memset(p, 0xab, size);
		ASSERT_EQ_INT(0xab, p[size - 1]);
		munmap(p, size);
		close(fd);
	}

	TEST("zero size succeeds, absurd size fails")
	{
		int fd = barny_shm_create(0);

		ASSERT_TRUE(fd >= 0);
		close(fd);
		ASSERT_EQ_INT(-1, barny_shm_create((size_t)-1));
	}

	TEST_SUITE_END();
}
