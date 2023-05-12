//
// Created by dario on 01/01/21.
//

#ifndef PROGETTO_CLION_COMMON_H
#define PROGETTO_CLION_COMMON_H
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <stdbool.h>

#undef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#undef min
#define min(a, b) (((a) < (b)) ? (a) : (b))

/**
 * Tempo massimo che può trascorrere da l'ultimo heartbeat
 * prima che il DS deve disconnettere il peer OVVERO
 * il peer si deve disconnettere
 */
#define MAX_HEARTBEAT_PERIOD    (struct timeval) { \
	.tv_sec = 30 \
}

#define PEER_DEFINITIVE_ID_MAX 0x07ff

typedef uint16_t peer_definitive_id;
typedef uint64_t peer_session_id;

/**
 * Converte la tupla <indirizzo IPv4, porta> in un solo intero
 * @param sock_addr
 * @return
 */
__always_inline peer_session_id peer_make_id(struct sockaddr_in const *const sock_addr)
{
	return ((uint64_t)(ntohl(sock_addr->sin_addr.s_addr)) << 16) + ntohs(sock_addr->sin_port);
}

__always_inline struct sockaddr_in peer_unmake_id(peer_session_id sid)
{
	return (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl((sid >> 16)),
		.sin_port = htons(sid) // Troncamento
	};
}

/**
 * Controllo se due indirizzi IPv4 sono eguali
 * @return a == b
 */
__always_inline bool sockaddr_in_equal(struct sockaddr_in const *const a, struct sockaddr_in const *const b)
{
	return peer_make_id(a) == peer_make_id(b);
}

__always_inline uint64_t htong(uint64_t x)
{
	if(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
		return ((uint64_t)htonl(x & 0xFFFFFFFF) << 32) | htonl((x) >> 32);
	else
		return x;
}

__always_inline uint64_t ntohg(uint64_t x)
{
	return htong(x);
}

/**
 * Semplice select su stdin
 * @return 1 se si può read() su stding
 */
__always_inline bool stdin_ready()
{
	fd_set rd;
	struct timeval t = {0};
	FD_ZERO( &rd );
	FD_SET( STDIN_FILENO, &rd );
	return 1 == select( STDIN_FILENO + 1, &rd, 0, 0, &t );
}

#endif //PROGETTO_CLION_COMMON_H
