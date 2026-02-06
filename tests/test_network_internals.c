/*
 * Tests for static functions in network.c
 * We #include the source file directly to access static functions.
 */

#include "test_framework.h"

/* Include network.c directly to access static functions */
#include "../src/modules/network.c"

void
test_is_physical_interface(void)
{
	TEST_SUITE_BEGIN("is_physical_interface");

	/* Interfaces that should be rejected */
	TEST("rejects loopback 'lo'")
	{
		ASSERT_FALSE(is_physical_interface("lo"));
	}

	TEST("rejects veth interfaces")
	{
		ASSERT_FALSE(is_physical_interface("veth1234abc"));
	}

	TEST("rejects docker interfaces")
	{
		ASSERT_FALSE(is_physical_interface("docker0"));
	}

	TEST("rejects bridge interfaces 'br-'")
	{
		ASSERT_FALSE(is_physical_interface("br-abc123"));
	}

	TEST("rejects virtual bridge 'virbr'")
	{
		ASSERT_FALSE(is_physical_interface("virbr0"));
	}

	/* Interfaces that should be accepted */
	TEST("accepts eth0")
	{
		ASSERT_TRUE(is_physical_interface("eth0"));
	}

	TEST("accepts enp0s3 (predictable naming)")
	{
		ASSERT_TRUE(is_physical_interface("enp0s3"));
	}

	TEST("accepts wlan0")
	{
		ASSERT_TRUE(is_physical_interface("wlan0"));
	}

	TEST("accepts wlp2s0 (predictable naming)")
	{
		ASSERT_TRUE(is_physical_interface("wlp2s0"));
	}

	TEST_SUITE_END();
}
