#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

extern int tests_run;
extern int tests_failed;

#define TEST(name) void name(void)

#define ASSERT_EQ(expected, actual) do { \
	tests_run++; \
	if ((expected) != (actual)) { \
		printf("FAIL: %s:%d - Expected %d, got %d\n", \
			__FILE__, __LINE__, (expected), (actual)); \
		tests_failed++; \
	} \
} while (0)

#define RUN_TEST(test) do { \
	printf("Running %s...\n", #test); \
	test(); \
} while(0);

#endif
