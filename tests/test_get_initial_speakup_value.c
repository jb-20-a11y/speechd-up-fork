#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "speechd-up-utils.h"

/* Test: round-trip for every valid input and selected edge cases. Exits non-zero on failure. */

static void test_round_trip_all(void)
{
	for (int num = -100; num <= 100; ++num) {
		int ret, base;
		get_initial_speakup_value(num, &ret, &base);

		// basic invariants
		if (!(ret >= 0 && ret <= 9)) {
			fprintf(stderr, "ret out of range for num=%d: ret=%d\n", num, ret);
			exit(EXIT_FAILURE);
		}
		if (!(base >= 0 && base <= 20)) {
			fprintf(stderr, "base out of range for num=%d: base=%d\n", num, base);
			exit(EXIT_FAILURE);
		}
		// base==20 only valid for num==100 and ret==9
		if (base == 20) {
			if (!(ret == 9 && num == 100)) {
				fprintf(stderr, "invalid base=20 for num=%d ret=%d\n", num, ret);
				exit(EXIT_FAILURE);
			}
		}

		int recomputed = (ret * 20) + base - 100;
		if (recomputed != num) {
			fprintf(stderr,
				"Round-trip mismatch for num=%d: ret=%d base=%d recomputed=%d\n",
				num, ret, base, recomputed);
			exit(EXIT_FAILURE);
		}
	}
}

static void test_selected_edges(void)
{
	int checks[] = { -100, -99, -81, -80, -79, -1, 0, 1, 19, 20, 21, 99, 100 };
	for (size_t i = 0; i < sizeof(checks)/sizeof(checks[0]); ++i) {
		int num = checks[i];
		int ret, base;
		get_initial_speakup_value(num, &ret, &base);
		int recomputed = (ret * 20) + base - 100;
		if (recomputed != num) {
			fprintf(stderr, "Edge round-trip failed for %d (ret=%d base=%d -> %d)\n",
					num, ret, base, recomputed);
			exit(EXIT_FAILURE);
		}
	}
}

int main()
{
	test_round_trip_all();
	test_selected_edges();
	printf("All get_initial_speakup_value tests passed.\n");
	return 0;
}
