#include "test_framework.h"

TEST(test_feature1) {
	ASSERT_EQ(5, 2 + 3);
}

void run_module1_tests(void) {
	RUN_TEST(test_feature1);
}
