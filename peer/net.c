//
// Created by dario on 07/01/21.
//

#include <errno.h>
#include "../shared/protocol.h"
#include "../shared/utility.h"
#include "../error.h"
#include "net.h"

struct ds_connection connect_to_ds(int fd_udp_socket, struct sockaddr_in *ds_address, peer_definitive_id id)
{
	uint8_t buffer_out[PROTOCOL_P2D_PACKET_SIZE];
	uint8_t buffer_in [PROTOCOL_P2D_PACKET_SIZE];
	struct sockaddr_in from;
	socklen_t from_len = sizeof(struct sockaddr_in);
	ssize_t ret;
	struct ds_connection con = {0};
	fd_set fd_set, fd_ready_set;

	// Creo l'insieme di descrittori per la select
	FD_ZERO(&fd_set);
	FD_SET(fd_udp_socket, &fd_set);

	for(unsigned int i = 0; !con.connected && i < 5; i++)
	{
		// Invio messaggio di registrazione al ds
		protocol_p2d_prepare(buffer_out, &(struct protocol_p2d_packet){
			.type = TYPE_PEER_WANTS_REGISTRATION,
			.peer_wants_registration.id = id
		});
		ret = sendto(fd_udp_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *)ds_address, sizeof(struct sockaddr_in));
		if(ret == -1)
			exit_throw("Invio messaggio di registrazione a ds", errno);

		// Attendo risposta dal ds
		fd_ready_set = fd_set;
		ret = select(fd_udp_socket + 1, &fd_ready_set, 0,0,&(struct timeval) {0, 5000});
		if(ret == -1)
			exit_throw("Errore su select() durante registrazione", errno);

		if(ret == 0)
		{
			print_warning("Tentativo %u caduto nel vuoto...", i);
			continue;
		}

		ret = recvfrom(fd_udp_socket, buffer_in, PROTOCOL_P2D_PACKET_SIZE, 0, (struct sockaddr *) &from, &from_len);
		if(ret == -1)
			exit_throw("Ricezione messaggio di ack del ds", errno);

		if(!sockaddr_in_equal(&from, ds_address))
		{
			print_warning("Ignoro pacchetto da %s, perché non arriva dal ds", sockaddr_in_to_string(&from));
			continue;
		}

		if(ret != PROTOCOL_P2D_PACKET_SIZE)
		{
			print_warning("Ignoro pacchetto di dimensione %ld, che è errata", ret);
			continue;
		}

		struct protocol_p2d_packet x = protocol_p2d_parse(buffer_in);
		if(x.type != TYPE_DS_ACK_REGISTRATION)
		{
			print_warning("Ignoro pacchetto di tipo %d != TYPE_DS_ACK_REGISTRATION", x.type);
			continue;
		}

		// 0k ora sia connessi
		// Imposto le variabili di stato
		con.connected = true;
		con.ds_address = *ds_address;
		con.my_id = x.ds_ack_registration.id;
		con.my_address_from_the_ouside = peer_unmake_id(x.ds_ack_registration.peer_address_as_seen_from_the_outside);
	}

	if(!con.connected)
	{
		print_error("Connessione a %s, fallita", sockaddr_in_to_string(ds_address));
		return con;
	}

	protocol_p2d_prepare(buffer_out, &(struct protocol_p2d_packet){
		.type = TYPE_PEER_ACK_REGISTRATION
	});
	sendto(fd_udp_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *)ds_address, sizeof(struct sockaddr_in));

	return con;
}

/*void connect_to_neighbors(struct ds_connection *connection, struct sockaddr_in *neighbor_in, struct sockaddr_in *neighbor_out)
{

}*/

void protocol_control_prepare_packet(uint8_t *buffer, struct control_packet const *packet)
{
	uint64_t *buf = (uint64_t *) buffer;
	// Tutto a 0
	memset(buffer, 0x00, CONTROL_PACKET_SIZE);

	buffer[0] = packet->type;
	switch(packet->type)
	{
	case TYPE_SEND_QUERY:
		buf[1] = htong(packet->data.send_query.aggr);
		buf[2] = htong(packet->data.send_query.type);
		buf[3] = htong(packet->data.send_query.start);
		buf[4] = htong(packet->data.send_query.end);
		break;
	case TYPE_SEND_QUERY_RESULT:
		buf[1] = htong(packet->data.send_query_result.status);
		buf[2] = htong(packet->data.send_query_result.result);
		break;
	case TYPE_ASK_FOR_ENTRIES:
		buf[1] = htong(packet->data.ask_for_entries.recipient);
		buf[2] = htong(packet->data.ask_for_entries.target);
		buf[3] = htong(packet->data.ask_for_entries.lower_bound);
		buf[4] = htong(packet->data.ask_for_entries.upper_bound);
		buf[5] = htong(packet->data.ask_for_entries.flood_ref);
		buf[6] = htong(packet->data.ask_for_entries.hops);
		break;
	case TYPE_TEST_RING:
		buf[1] = htong(packet->data.test.sender);
		buf[2] = htong(packet->data.test.hops);
		break;
	case TYPE_REPLY_LOWEST_LOWER_BOUND:
		buf[1] = htong(packet->data.reply_lowest_lower_bound.lower_bound);
	case TYPE_CONTROL_INVALID:
	case TYPE_ASK_FOR_LOWEST_LOWER_BOUND:
	default:
		break;
	}
}

struct control_packet protocol_control_parse(uint8_t const *const buffer)
{
	// Per comodità
	uint64_t const *const packet = (uint64_t const *) buffer;
	struct control_packet content = {0};

	content.type = buffer[0];
	switch(content.type)
	{
	case TYPE_SEND_QUERY:
		content.data.send_query.aggr = ntohg(packet[1]);
		content.data.send_query.type = ntohg(packet[2]);
		content.data.send_query.start = ntohg(packet[3]);
		content.data.send_query.end = ntohg(packet[4]);
		break;
	case TYPE_SEND_QUERY_RESULT:
		content.data.send_query_result.status = ntohg(packet[1]);
		content.data.send_query_result.result = ntohg(packet[2]);
		break;
	case TYPE_ASK_FOR_ENTRIES:
		content.data.ask_for_entries.recipient = ntohg(packet[1]);
		content.data.ask_for_entries.target = ntohg(packet[2]);
		content.data.ask_for_entries.lower_bound = ntohg(packet[3]);
		content.data.ask_for_entries.upper_bound = ntohg(packet[4]);
		content.data.ask_for_entries.flood_ref = ntohg(packet[5]);
		content.data.ask_for_entries.hops = htong(packet[6]);
		break;
	case TYPE_TEST_RING:
		content.data.test.sender = htong(packet[1]);
		content.data.test.hops = htong(packet[2]);
		break;
	case TYPE_REPLY_LOWEST_LOWER_BOUND:
		content.data.reply_lowest_lower_bound.lower_bound = ntohg(packet[1]);
	case TYPE_CONTROL_INVALID:
	default:
		break;
	}

	return content;
}

struct data_register_entry parse_data_register_entry(uint64_t const *const packet)
{
	return (struct data_register_entry) {
		.author = ntohg(packet[0]),
		.type = ntohg(packet[1]),
		.quantity = ntohg(packet[2]),
		.date = ntohg(packet[3])
	};
}

void prepare_data_register_entry(uint64_t *packet, struct data_register_entry const *const info)
{
	packet[0] = htong(info->author);
	packet[1] = htong(info->type);
	packet[2] = htong(info->quantity);
	packet[3] = htong(info->date);
}