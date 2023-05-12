//
// Created by dario on 29/01/21.
//

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include "query.h"
#include "disk.h"
#include "net.h"

#define QUERIES_CACHE_SIZE 100

// aggr == 0 se la cella non contiene dati validi
static struct query computed_queries[QUERIES_CACHE_SIZE] = {{0}};
static struct query_result computed_queries_result[QUERIES_CACHE_SIZE] = {{0}};

// Considerare sempre in modulo QUERIES_CACHE_SIZE
static size_t computed_queries_next_slot = 0;
static bool array_fill = 0;

struct query_result query_find_in_cache(const struct query q)
{
	const size_t len_q = array_fill ? QUERIES_CACHE_SIZE : computed_queries_next_slot;

	if(q.aggr == VARIATION)
		return (struct query_result) {.status = INVALID_RESULT};

	// Ricerco tra le query già calcolate
	for(size_t i = 0; i < len_q; i++)
		if(computed_queries[i].aggr != INVALID && !memcmp(&q, computed_queries + i, sizeof(struct query)))
		{
			print_query_log("Interrogazione trovata in cache!%s", "");
			computed_queries_result[i].status = SUCCESS;
			return computed_queries_result[i];
		}
	return (struct query_result) {.status = INVALID_RESULT};
}

/**
 * Richiesta di FLOODING al network
 * @param query
 * @param neighboor_out
 * @param missing
 */
static void query_send_control_missing_entries(struct query query, struct ds_connection *dc, int neighboor_out, struct peer_node **missing, time_t ref);

struct query_result query_compute(struct query q, struct disk_status *const dks, struct ds_connection *const dc, int neighbour_in, int neighbour_out, struct peer_node **missing, int flags)
{
	struct query_result res = {0};
	uint8_t buffer[CONTROL_PACKET_SIZE];
	ssize_t ret;
	int neighbours[2] = {neighbour_in, neighbour_out};

	// I flags su quali parti saltare
	bool check_cache = !(flags & QUERY_COMPUTE_SKIP_CACHE);
	bool send_neihbours = !(flags & QUERY_COMPUTE_SKIP_NEIGHBOURS) && neighbour_out != -1 && neighbour_in != -1  && q.aggr == TOTAL;
	bool make_flood = !(flags & QUERY_COMPUTE_SKIP_FLOODING);

	if(q.start >= q.end)
		return res;

	// È in cache?
	if(check_cache)
	{
		res = query_find_in_cache(q);
		if(res.status != INVALID_RESULT)
			return res;
		else
			res = (struct query_result){0};
	}

	// Chiedo ai miei vicini se hanno la query già calcolata.
	protocol_control_prepare_packet(buffer, &(struct control_packet) {
		.type = TYPE_SEND_QUERY,
		.data.send_query = q
	});
	for(int i = 0; send_neihbours && i < 2; i++)
	{
		ret = send(neighbours[i], buffer, CONTROL_PACKET_SIZE, MSG_WAITALL);
		if(ret < CONTROL_PACKET_SIZE)
			exit_throw("send() al vicino", errno);
	}

	if(send_neihbours)
		// La reference va rivista
		return (struct query_result) {.status = MISSING_ENTIRES_ASK_NET_CACHE, .reference = q.start ^ q.end};

	// Pulisco la lista
	peer_list_delete(*missing);
	*missing = 0;

	// Controllo se ho tutti i dati necessari a proseguire
	for(peer_definitive_id i = 1; neighbour_out != -1 && make_flood && i <= dc->max_id; i++)
	{
		time_t last_update = peers_register_get_last_update(dks, i);
		//printf("=========== %lu \t %lu\n", last_update, q.end);
		if (last_update <= q.end && i != dks->my_id)
		{
			// Mi salvo in una lista i nodi mancanti così che
			// 1. potrò inviare tutti i pacchetti di controllo in una volta
			// 2. l'automata potrà rimuove i nodi che completano il flooding
			//      e dichiarare l'interrogazione come terminata a lista vuota
			struct peer_node *node = peer_list_add(missing, i, i);
			node->value.last_update = last_update;
		}
	}

	// Mancano dati e c'è qualcuno a cui chiedereli
	if(*missing && neighbour_out != -1)
	{
		res.reference = time(0);
		res.status = MISSING_ENTRIES_FLOOD;

		query_send_control_missing_entries(q, dc, neighbour_out, missing, res.reference);
		return res;
	}

	// Ciclo per tutti i giorni da start a end
	for(time_t current_day = q.start; current_day <= q.end; current_day += 60*60*24)
	{
		struct data_register reg = open_data_register(dks, current_day);
		long long tot = 0;

		for(size_t i = 0; i < reg.length; i++)
		{
			if(reg.entries[i].type == q.type)
				tot += reg.entries[i].quantity;
		}

		if(q.aggr == TOTAL)
			res.result += tot;
		else
		{
			printf("» %lld\n", tot - res.result);
			res.result = tot;
		}

		close_data_register(&reg);
	}

	res.status = SUCCESS;

	// Le query del futuro non sono salvate / su register aperti
	time_t now = time(0);
	if(q.end <= now - (now % (60 * 60 * 24)))
	{
		computed_queries[computed_queries_next_slot % QUERIES_CACHE_SIZE] = q;
		computed_queries_result[computed_queries_next_slot % QUERIES_CACHE_SIZE] = res;
		computed_queries_next_slot++;
	}
	else
		print_warning("Salto memorizzazione in cache perché %lu è anteriore ai registri chiusi", q.end);
	return res;
}

static const char *status_strings[] = {"INVALIDO", "SUCCESSO", "VOCI DI REGISTRO MANCANTI: INTERROGAZIONE CACHE VICINI" ,"VOCI DI REGISTRO MANCANTI: FLOODING"};

void dprintqueryresult(int fd_printer, struct query_result const *const qr)
{
	dprintf(fd_printer, "\e[5;1m############## RISULTATO INTERROGAZIONE #################\e[0m\n");
	dprintf(fd_printer,"Stato:\t%s\n", qr->status < 4 ? status_strings[qr->status] : "???");
	switch(qr->status)
	{
	case INVALID_RESULT:
		break;
	case SUCCESS:
		dprintf(fd_printer, "Totale: %llu\n", qr->result);
		break;
	case MISSING_ENTRIES_FLOOD:
	case MISSING_ENTIRES_ASK_NET_CACHE:
		dprintf(fd_printer, "Riferimento: %lu\n", qr->reference);
		break;
	}
	dprintf(fd_printer, "\e[5;1m########################################################\e[0m\n\n\n");
}

static void query_send_control_missing_entries(struct query query, struct ds_connection *const dc, int neighboor_out, struct peer_node **missing, time_t ref)
{
	int ret;
	uint8_t buffer[CONTROL_PACKET_SIZE];

	if(!*missing)
		exit_throw("Che cosa? missing è nullptr!", ENODATA);

	for(struct peer_node *t = *missing; t; t = t->greater_eq)
	{
		protocol_control_prepare_packet(buffer, &(struct control_packet) {
			.type = TYPE_ASK_FOR_ENTRIES,
			.data.ask_for_entries.recipient = peer_make_id(&dc->my_address_from_the_ouside),
			.data.ask_for_entries.target = t->value.id,
			.data.ask_for_entries.lower_bound = min(query.end, t->value.last_update), // Non dovrebbe essere necessario. Basta t->value.last_update
			.data.ask_for_entries.upper_bound = query.end,
			.data.ask_for_entries.flood_ref = ref
		});
		ret = send(neighboor_out, &buffer, CONTROL_PACKET_SIZE, MSG_WAITALL);
		if(ret == -1)
			exit_throw("send()", errno);
	}
}
