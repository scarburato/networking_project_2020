//
// Created by dario on 05/01/21.
//

#ifndef PROGETTO_CLION_UTILITY_H
#define PROGETTO_CLION_UTILITY_H

#include <sys/time.h>
#include <arpa/inet.h>
#include <stdbool.h>

/**
 * Subtract the `struct timeval' values X and Y,
 * storing the result in RESULT.
 * @return 1 ←→ x < y ←→ The difference is negative
 **/
int timeval_subtract (struct timeval *result, struct timeval x, struct timeval y);

__always_inline bool timeval_equal(struct timeval a, struct timeval b)
{
	return a.tv_usec == b.tv_usec && a.tv_sec == b.tv_sec;
}

/**
 * Stampo su una linea di stdout la coppia (IPv4, porta)
 * @param sockaddr_in
 */
char * sockaddr_in_to_string(const struct sockaddr_in *const address);

#endif //PROGETTO_CLION_UTILITY_H
