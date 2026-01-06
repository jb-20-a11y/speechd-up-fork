#include <assert.h>
#include "speechd-up-utils.h"

void get_initial_speakup_value(int num, int *ret, int *base)
{
	assert((num >= -100) && (num <= 100));
	int val = num + 100;
	*ret = val / 20;
	if (*ret == 10) {
		*ret = 9;
		*base = 20;
	} else
		*base = val % 20;
	assert((*ret >= 0) && (*ret <= 9));
}
