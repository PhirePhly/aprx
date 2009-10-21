#include <stdio.h>

static int aprspass(const char *mycall)
{
	int a = 0, h = 29666, c;

	for (; *mycall; ++mycall) {
		c = 0xFF & *mycall;
		if (!(('0' <= c && c <= '9') || ('A' <= c && c <= 'Z')))
			break;
		h ^= ((0xFF & *mycall) * (a ? 1 : 256));
		a = !a;
	}
	return h;
}

main()
{
	printf("APRSPASS: %d\n", aprspass("OH2MQK-1"));
	return 0;
}
