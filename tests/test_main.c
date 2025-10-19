#include "test_framework.h"

int tests_run = 0;
int tests_failed = 0;

// Declare test suite runners
extern void run_module1_tests(void);

int main(void) {
	printf("=== Running Tets Suite ===\n\n");

	run_module1_tests();

	printf("\n=== Test Results ===\n");
	printf("Tests run: %d\n", tests_run);
	printf("Tests failed: %d\n", tests_failed);
	printf("Tests passed: %d\n", tests_run - tests_failed);

	return tests_failed > 0 ? 1 : 0;
}
