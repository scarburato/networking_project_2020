//
// Created by dario on 07/01/21.
//

#ifndef PROGETTO_CLION_NET_H
#define PROGETTO_CLION_NET_H

#include <arpa/inet.h>
#include <stdbool.h>
#include <sys/time.h>
#include "../common.h"
#include "console_logic.h"
#include "types.h"

#define PEER_PUSH_ENTRY_BUFFER_SIZE 0x10
#define CONTROL_PACKET_SIZE 0x08 * 7
#define MAX_HOPS PEER_DEFINITIVE_ID_MAX + 2

struct ds_connection
{
	bool                    connected;
	struct sockaddr_in      ds_address;
	struct timeval          last_heartbeat;

	peer_definitive_id      my_id;
	peer_definitive_id      max_id;

	struct sockaddr_in      my_address_from_the_ouside;
};

/**
 * Funzione bloccante. Cerca di registrarsi al DS. Durante i tentativi di handshake ogni
 * altro pacchetto in ingresso sul socket udp viene scartato.
 * @param fd_udp_socket
 * @param ds_address
 * @param id
 * @return
 */
struct ds_connection connect_to_ds(int fd_udp_socket, struct sockaddr_in *ds_address, peer_definitive_id id);

/**
 * I tipi di connessione del peer
 * - CONTROL per le comunicazioni sui vicini: in particolare richieste di query
 * 	o di aggiornamento
 * - DAT per quanto chi fa la connect() vuole
 */
enum peer_connection_type {CONTROL = 0x01, DATA};
enum control_packet_type
{
	TYPE_CONTROL_INVALID = 0x00,

	TYPE_SEND_QUERY,
	TYPE_SEND_QUERY_RESULT,

	TYPE_ASK_FOR_ENTRIES,

	TYPE_ASK_FOR_LOWEST_LOWER_BOUND,
	TYPE_REPLY_LOWEST_LOWER_BOUND,

	TYPE_TEST_RING = 0xaa
};

/**
 * Struttura che rappresenta il contentuo di un pacchetto di controllo inviato
 * sul canale TCP di tipo CONTROL
 */
struct control_packet
{
	enum control_packet_type type;
	union
	{
		struct query send_query;
		struct query_result send_query_result;
		struct {
			peer_session_id recipient;

			peer_definitive_id target;

			// I margini temporali: da quando a quando mi servono entries
			time_t lower_bound;
			time_t upper_bound;

			// Quando ò avviato
			time_t flood_ref;
			uint64_t hops;
		} ask_for_entries;
		struct{
			peer_definitive_id sender;
			uint64_t hops;
		} test;
		struct {
			time_t lower_bound;
		} reply_lowest_lower_bound;
	} data;
};

/**
 * Prepara un pacchetto di controllo
 * @param buffer di PEER_PUSH_ENTRY_BUFFER_SIZE bytes
 */
void protocol_control_prepare_packet(uint8_t *buffer, struct control_packet const *packet);

/**
 * Inverso di protocol_control_prepare_packet
 * @param buffer
 * @return
 */
struct control_packet protocol_control_parse(uint8_t const *const buffer);

struct data_register_entry parse_data_register_entry(uint64_t const *const packet);

/**
 *
 * @param packet buffer di 4 parole quadruple amd64
 * @param info
 */
void prepare_data_register_entry(uint64_t *packet, struct data_register_entry const *info);

#endif //PROGETTO_CLION_NET_H