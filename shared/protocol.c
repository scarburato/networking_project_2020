//
// Created by dario on 01/01/21.
//

#include <string.h>
#include <errno.h>
#include "protocol.h"

int protocol_p2d_prepare(uint8_t *const buffer, struct protocol_p2d_packet const *const content)
{
	// Per comodità
	uint64_t *const packet = (uint64_t*)buffer;

	// Per sicurezza azzero la memoria
	memset(buffer, 0x00,PROTOCOL_P2D_PACKET_SIZE);

	// Inizio processo
	packet[0] = content->type;

	switch (content->type)
	{
	case TYPE_PEER_WANTS_REGISTRATION:
		packet[1] = htons(content->peer_wants_registration.id);
		break;
	case TYPE_DS_ACK_REGISTRATION:
		packet[1] = htons(content->ds_ack_registration.id);
		packet[2] = htong(content->ds_ack_registration.peer_address_as_seen_from_the_outside);
		break;
	case TYPE_DS_UPDATE_NEIGHBORS:
		packet[1] = htong(content->ds_update_neighbors.time.tv_sec);
		packet[2] = htong(content->ds_update_neighbors.time.tv_usec);
		packet[3] = htong(content->ds_update_neighbors.current_max_id);
		packet[4] = htong(content->ds_update_neighbors.neighbor_out);
		packet[5] = htong(content->ds_update_neighbors.neighbor_in);
		break;
	case TYPE_PEER_ACK_NEIGHBORS:
		packet[1] = htong(content->peer_ack_neighbors.ref_time.tv_sec);
		packet[2] = htong(content->peer_ack_neighbors.ref_time.tv_usec);
		break;
	case TYPE_HEARTBEAT:
		packet[1] = htong(content->heartbeat.ref_time.tv_sec);
		packet[2] = htong(content->heartbeat.ref_time.tv_usec);
		break;
	case TYPE_PEER_ACK_REGISTRATION:
	case TYPE_DS_BUSY:
	case TYPE_DS_SHUTTING_DOWN:
	case TYPE_PEER_SHUTTING_DOWN:
		break;
	default:
		return EINVAL;
	}
	return 0;
}

struct protocol_p2d_packet protocol_p2d_parse(uint8_t const *const buffer)
{
	// Per comodità
	uint64_t const *const packet = (uint64_t const*)buffer;
	struct protocol_p2d_packet content = {0};

	content.type = (uint8_t)packet[0];

	switch (content.type)
	{
	case TYPE_PEER_WANTS_REGISTRATION:
		content.peer_wants_registration.id = ntohs(packet[1]);
		break;
	case TYPE_DS_ACK_REGISTRATION:
		content.ds_ack_registration.id = ntohs(packet[1]);
		content.ds_ack_registration.peer_address_as_seen_from_the_outside = htong(packet[2]);
		break;
	case TYPE_DS_UPDATE_NEIGHBORS:
		content.ds_update_neighbors.time.tv_sec = ntohg(packet[1]);
		content.ds_update_neighbors.time.tv_usec = ntohg(packet[2]);
		content.ds_update_neighbors.current_max_id = ntohg(packet[3]);
		content.ds_update_neighbors.neighbor_out = ntohg(packet[4]);
		content.ds_update_neighbors.neighbor_in = ntohg(packet[5]);
		break;
	case TYPE_PEER_ACK_NEIGHBORS:
		content.peer_ack_neighbors.ref_time.tv_sec = ntohg(packet[1]);
		content.peer_ack_neighbors.ref_time.tv_usec = ntohg(packet[2]);
		break;
	case TYPE_HEARTBEAT:
		content.heartbeat.ref_time.tv_sec = htong(packet[1]);
		content.heartbeat.ref_time.tv_usec = htong(packet[2]);
	case TYPE_PEER_ACK_REGISTRATION:
	case TYPE_DS_BUSY:
	case TYPE_DS_SHUTTING_DOWN:
	case TYPE_PEER_SHUTTING_DOWN:
		break;
	default:
		content.type = TYPE_INVALID;
		content.error = EINVAL;
	}

	return content;
}