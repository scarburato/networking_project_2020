//
// Created by dario on 01/01/21.
//

#ifndef PROGETTO_CLION_PROTOCOL_H
#define PROGETTO_CLION_PROTOCOL_H

#include <arpa/inet.h>
#include "peers_info.h"

#define PROTOCOL_P2D_PACKET_SIZE 8*6

enum protocol_p2d_packet_type
{
	TYPE_INVALID = 0,
	TYPE_PEER_WANTS_REGISTRATION = 1,
	TYPE_DS_ACK_REGISTRATION,
	TYPE_PEER_ACK_REGISTRATION,

	TYPE_DS_UPDATE_NEIGHBORS = 0x10, // salto il 4 senno Wireshark pensa che non sia udp...
	TYPE_PEER_ACK_NEIGHBORS,

	TYPE_HEARTBEAT,

	TYPE_DS_SHUTTING_DOWN,
	TYPE_PEER_SHUTTING_DOWN,

	// Non usato. Se il DS termina le risorse stampa un errore ed esce
	TYPE_DS_BUSY = 0xff
};

struct protocol_p2d_packet
{
	/** Usato solo dalla parse */
	int error;
	enum protocol_p2d_packet_type type;
	union
	{
		struct
		{
			peer_definitive_id id;
		} peer_wants_registration;
		struct {
			peer_definitive_id  id;
			peer_session_id peer_address_as_seen_from_the_outside;
		} ds_ack_registration;
		struct
		{
			struct timeval time;
			peer_definitive_id current_max_id;
			/** 0 se non ci sono i vicini */
			peer_session_id neighbor_out;
			/** 0 se non ci sono i vicini */
			peer_session_id neighbor_in;
		} ds_update_neighbors;
		struct
		{
			struct timeval ref_time;
		} peer_ack_neighbors, heartbeat;
	};
};

/**
 * Crea un pacchetto di tipo P2D.
 * @param buffer un buffer lungo almeno PROTOCOL_P2D_PACKET_SIZE
 * @param content Informazioni sul contenuto del pacchetto da processare
 * @return
 */
int protocol_p2d_prepare(uint8_t *buffer, struct protocol_p2d_packet const *content);

/**
 * Dato un pacchetto di tipo P2D lo processa e ne ritorna la rappresentazione
 * interna al programmas
 * @param buffer buffer lungo almeno PROTOCOL_P2D_PACKET_SIZE
 * @return
 */
struct protocol_p2d_packet protocol_p2d_parse(uint8_t const *buffer);

#endif //PROGETTO_CLION_PROTOCOL_H
