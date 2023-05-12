//
// Created by dario on 05/01/21.
//
#include <stdio.h>
#include "utility.h"

char print_inet[128] = {0};

/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.  */

int timeval_subtract (struct timeval *result, struct timeval x, struct timeval y)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x.tv_usec < y.tv_usec) {
		int nsec = (y.tv_usec - x.tv_usec) / 1000000 + 1;
		y.tv_usec -= 1000000 * nsec;
		y.tv_sec += nsec;
	}
	if (x.tv_usec - y.tv_usec > 1000000) {
		int nsec = (x.tv_usec - y.tv_usec) / 1000000;
		y.tv_usec += 1000000 * nsec;
		y.tv_sec -= nsec;
	}

	/* Compute the time remaining upper_bound wait.
	   tv_usec is certainly positive. */
	result->tv_sec = x.tv_sec - y.tv_sec;
	result->tv_usec = x.tv_usec - y.tv_usec;

	/* Return 1 if result is negative. */
	return x.tv_sec < y.tv_sec;
}

char * sockaddr_in_to_string(const struct sockaddr_in *const address)
{
	char *ip = inet_ntoa(address->sin_addr);
	unsigned short port = ntohs(address->sin_port);

	snprintf(print_inet, 128, "%s:\%hu", ip, port);
	return print_inet;
}
