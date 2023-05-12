#ifndef ERROR_H_INCLUDE
#define ERROR_H_INCLUDE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define print_notice(format, ...) \
	fprintf(stderr, "%s:%u\t\e[1;35mNota\e[0;95m " format "\e[0m\n", __FILE__, __LINE__ ,__VA_ARGS__)

#define print_warning(format, ...) \
	fprintf(stderr, "%s:%u\t\e[1;33mAttenzione\e[0;93m " format "\e[0m\n",__FILE__, __LINE__ , __VA_ARGS__)

#define print_error(format, ...) \
	fprintf(stderr, "%s:%u\t\e[1;31mErrore\e[0;91m " format "\e[0m\n",__FILE__, __LINE__ ,__VA_ARGS__)

#define print_query_log(format, ...) \
	fprintf(stderr, "%s:%u\t\e[1;32mQuery\e[0;92m " format "\e[0m\n",__FILE__, __LINE__ ,__VA_ARGS__)

#define print_errno(msg, err) \
	print_error("%s; codice: %d: %s", msg, err, strerror(err))

#define exit_throw(msg, err) \
	do {\
                print_errno(msg, err);\
                abort();\
	} while(0);

#endif