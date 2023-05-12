#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include "../common.h"
#include "../shared/protocol.h"
#include "../shared/utility.h"
#include "../error.h"
#include "console_logic.h"

#define MAX_PEERS_PENDING_REGISTRATION_ACK 200
#define DEFAULT_PORT 7575

struct peer_SD_assoc
{ // Struttura di 16 byte
	peer_session_id         socket_from;
	peer_definitive_id      given_id;
} ;

/**
 * Tempo massimo di attesa su socket UDP del DS.
 * Massimo 5ms attendo. Impostare valori troppo alti
 * può rallentare la lettura da telescrivente (stdin)
 * e la logica automatica
 */
static struct timeval recive_timeout = {
	.tv_sec = 0,
	.tv_usec = 5000
};

/**
 * Ogni quanto inviare heartbeat ai peers. È bene
 * non avere una frequenza troppo alta per evitare congestioni
 * rete
 */
static struct timeval heartbeat_period = {
	.tv_sec = 5
};

/**
 * Ogni quando devo rispedire i vicini se non ho ricevuto la ACK
 * dal peer
 */
static struct timeval update_neighboor_auto_retry_period = {
	.tv_usec = 500000 //500ms
};

// Terminare il programma
static bool terminato = false;

/**
 * Invia un pacchetto a target con i suoi vicini.
 * @param connected_peers	Lista di nodi
 * @param target		L'elemento della lista di nodi
 * @param fd_socket		Il socket su cui devo inviare il pacchetto
 * @param time			Il tempo corrente
 * @param max_id		Il prossimo id disponible
 * @return -1 se errore. 0 altrimenti
 */
static int update_neighbors_of_target(struct peer_node *connected_peers, struct peer_node *target, int fd_socket, struct timeval time, peer_definitive_id max_id);

int main(int argc, char** argv)
{
	// Voglio gestire in sincronia gli errori del tubo (es. connessione chiusa senza eof)
	signal(SIGPIPE, SIG_IGN);
	int ret;

	/** Variabili di stato del DS */
	/**
	 * Il prossimo id assegnable.
	 * Per semplicità i peers non si possono cancellare una volta registrati, in questo modo
	 * so anche quanti peer ci sono (next_free_id - 1) e quali sono i loro ID (da 0 a next_free_id-1)
	 * Per semplicità, il ds non si salva su disco le informazioni su quanti peer fanno parte della
	 * sua rete ma proverà a leggerle dalla variabile d'ambiente DS_PEERS_CARDINALITY
	 */
	peer_definitive_id next_free_id = getenv("DS_PEERS_CARDINALITY") ? atoi(getenv("DS_PEERS_CARDINALITY")) + 1 : 10;
	struct ds_console console_state = {0};

	// Lista dei peer attualmente collegati
	struct peer_node *connected_peers = 0;

	// I peer che pendono ack
	struct peer_SD_assoc peers_pending_registration_ack[MAX_PEERS_PENDING_REGISTRATION_ACK] = {{0}};
	size_t peers_pending_registration_ack_len = 0;

	/** Timer */
	struct timeval last_heartbeat = {0};
	struct timeval current_time;
	struct timeval diff_time;

	/** Roba di IO I/O*/
	uint8_t buffer_out[PROTOCOL_P2D_PACKET_SIZE];
	uint8_t buffer_in[PROTOCOL_P2D_PACKET_SIZE];
	struct sockaddr_in listen_address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(argc >= 2 ? atoi(argv[1]) : DEFAULT_PORT)
	};

	int fd_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd_socket == -1)
		exit_throw("Creazione del socket", errno);

	// Imposto un timeout in ricezione
	ret = setsockopt(fd_socket, SOL_SOCKET, SO_RCVTIMEO, &recive_timeout, sizeof(struct timeval));
	if(ret == -1)
		exit_throw("Impostazione del tempo di scadenza sul socket", errno);

	ret = bind(fd_socket, (const struct sockaddr *) &listen_address, sizeof(struct sockaddr_in));
	if(ret == -1)
		exit_throw("Bind del socket all'indirizzo di ascolto", errno);

	fprintf(stderr, "Tutto 0k! Peer avviato\n");

	while(!terminato) {
		struct protocol_p2d_packet      ds_reply = {0};
		struct peer_node                *neighbors[2] = {0};

		// Mi salvo a che ora inizio il ciclo
		gettimeofday(&current_time, 0);
		// Se la chiamo troppe volte al millisecondo il Kernel mi rifiuta..
		if(ret == -1 && errno != EAGAIN && errno != EINTR)
			exit_throw("Esecuzione di gettimeofday()", errno);

		/***************************************************************
		 *               LOGICA DELLA DISCONESSIONE AUTOMATICA
		*****************************************************************/
		for(struct peer_node *i = connected_peers; i;)
		{
			struct timeval first_diff_time;
			timeval_subtract(&first_diff_time, current_time, i->value.last_heartbeat);
			ret = timeval_subtract(&diff_time, first_diff_time, MAX_HEARTBEAT_PERIOD);

			// current_time >= i->value.last_heartbeat + MAX_HEARTBEAT_PERIOD
			// Se la diseq è falsa allora è ancora vivo, probabilemtne.
			if(ret == 1 || (i->value.last_heartbeat.tv_usec == 0 && i->value.last_heartbeat.tv_sec == 0))
			{
				i = i->greater_eq;
				continue;
			}

			struct sockaddr_in t = peer_unmake_id(i->address_id);
			struct peer_node *me = i;
			i = i->greater_eq;

			print_warning(
				"Il nodo %u su indirizzo %s non dà segni di vita da %ld secondi e %ld microsecondi. È tempo di staccarlo\n",
				me->value.id, sockaddr_in_to_string(&t), first_diff_time.tv_sec, first_diff_time.tv_usec
			);

			// Invio il segnale di arressto al peer, magari lo riceve
			ds_reply.type = TYPE_DS_SHUTTING_DOWN;
			protocol_p2d_prepare(buffer_out, &ds_reply);
			sendto(fd_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &t, sizeof(struct sockaddr_in));

			// cancello il peer dalla lista
			peer_list_remove(&connected_peers, me);

			// Se la lista è diventata vuota i puntatori qua sotto sono stati liberati e non ci sono peer registrati...
			if(!connected_peers)
				break;

			for(struct peer_node *i = connected_peers; i; i = i->greater_eq)
				update_neighbors_of_target(connected_peers, i, fd_socket, current_time, next_free_id);
		}

		/***************************************************************
		 *               LOGICA DEGLI INVII AUTONOMI
		*****************************************************************/
		// Devo inviare il battito ?
		timeval_subtract(&diff_time, current_time, last_heartbeat);
		ret = timeval_subtract(&diff_time, diff_time, heartbeat_period);

		// Invio i il segnale solo se current_time >= last_heartbeat + heartbeat_period
		if(ret == 0)
		{
			last_heartbeat = current_time;

			ds_reply.type = TYPE_HEARTBEAT;
			ds_reply.heartbeat.ref_time = current_time;
			protocol_p2d_prepare(buffer_out, &ds_reply);

			for(struct peer_node *i = connected_peers; i; i = i->greater_eq)
			{
				struct sockaddr_in dest = peer_unmake_id(i->address_id);
				ret = sendto(fd_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &dest, sizeof(struct sockaddr_in));
				if(ret == -1)
					exit_throw("Invio di heartbeat", errno);
			}
		}

		// Se ci sono peer che di cui non ho ricevuto ack da update_neighboor_auto_retry_period
		for(struct peer_node *i = connected_peers; i; i = i->greater_eq)
		{
			if(timeval_equal(i->value.last_event, (struct timeval){0}))
				continue;

			timeval_subtract(&diff_time, current_time, i->value.last_event);
			ret = timeval_subtract(&diff_time, diff_time, update_neighboor_auto_retry_period);

			// current_time <= last_event + auto_try_period
			if(ret != 0)
				continue;

			update_neighbors_of_target(connected_peers, i, fd_socket, current_time, next_free_id);
		}

		/***************************************************************
		 *               LOGICA DELLA CONSOLE
		*****************************************************************/
		enum ds_console_command command = ds_console_dispatcher(&console_state);
		struct peer_node *target = 0;
		switch (command)
		{
		case CONSOLE_QUIT:
			print_notice("È stato richiesta la chiusura. %u", next_free_id);
			terminato = true;
			continue;
			break;
		case CONSOLE_COMMAND_SHOW_PEERS:
			puts("ID\tIPv4:porta udp");
			for(struct peer_node *i = connected_peers; i; i = i->greater_eq)
			{
				struct sockaddr_in t = peer_unmake_id(i->address_id);
				printf("%u\t%s\n", i->value.id, sockaddr_in_to_string(&t));
			}
			break;
		case CONSOLE_COMMAND_SHOW_NEIGHBOR:
			for(struct peer_node *i = connected_peers; !target && i; i = i->greater_eq)
				if(i->value.id == console_state.show_neighbor_of_id)
					target = i;
			if(!target)
				print_error("Il peer %u, non esiste...", console_state.show_neighbor_of_id);
			else
			{
				struct sockaddr_in a, b;
				peer_list_get_neighbors(connected_peers, target, neighbors);
				a = peer_unmake_id(neighbors[0]->address_id);
				b = peer_unmake_id(neighbors[1]->address_id);
				puts("ID\tIPv4:porta udp");
				printf("%s\n", sockaddr_in_to_string(&a));
				printf("%s\n", sockaddr_in_to_string(&b));
			}
			break;
		default:
			break;
		}

		/****************************************************************
		 *               LOGICA DI RECVFROM
		 ***************************************************************/
		struct sockaddr_in              from_inet_address = {0};
		socklen_t                       from_inet_address_size = sizeof(struct sockaddr_in);
		struct protocol_p2d_packet      recived_packet;
		size_t                          ack_index = SIZE_MAX;
		struct peer_node                *work_node;

		ret = recvfrom(fd_socket, buffer_in, PROTOCOL_P2D_PACKET_SIZE, 0, (struct sockaddr *) &from_inet_address, &from_inet_address_size);
		// EAGAIN vuol dire che sono andato in timeout di lettura
		if(ret == -1 && errno != EAGAIN && errno != EINTR)
			exit_throw("Lettura da socket udp", errno);

		if(ret == -1)
			continue;

		recived_packet = protocol_p2d_parse(buffer_in);
		switch(recived_packet.type)
		{
		case TYPE_PEER_WANTS_REGISTRATION:
			// Controllo se questo peer è già stato connesso.
			if(peer_list_find(connected_peers, peer_make_id(&from_inet_address)))
			{
				print_warning("Ignoro richiesta di registrazione da %lu. Risulta già connesso!", peer_make_id(&from_inet_address));
				break;
			}

			print_notice("Connessione in ingresso da %s", sockaddr_in_to_string(&from_inet_address));

			// Controllo se ò già altre richieste in sospeso da questo peer
			for(size_t i = 0; !ds_reply.ds_ack_registration.id && i < peers_pending_registration_ack_len; i++)
				if (peer_make_id(&from_inet_address) == peers_pending_registration_ack[i].socket_from)
					ds_reply.ds_ack_registration.id = peers_pending_registration_ack[i].given_id;

			// Se questo peer non è già connesso ovvero non à altre richieste in sospeso creo un id
			// e lo tra i ds_ack_registration_id ovvero uso il suo, se è possibile;
			if(!ds_reply.ds_ack_registration.id)
			{
				if(!recived_packet.peer_wants_registration.id || recived_packet.peer_wants_registration.id >= next_free_id)
				{
					if(recived_packet.peer_wants_registration.id > next_free_id)
						print_warning("Il peer si voleva collegare con id=%u ma è troppo alto, gli è stato assegnato %u", recived_packet.peer_wants_registration.id, next_free_id);

					ds_reply.ds_ack_registration.id = next_free_id;
					next_free_id++;
				}
				else
					ds_reply.ds_ack_registration.id = recived_packet.peer_wants_registration.id;

				// Meglio non uscire fuori dalla memoria...
				// @FIXME invia paccheto TYPE_DS_BUSY per segnalare che sono occupato invece di terminare il programma
				if(peers_pending_registration_ack_len == MAX_PEERS_PENDING_REGISTRATION_ACK)
					return ENOMEM;

				peers_pending_registration_ack[peers_pending_registration_ack_len].socket_from = peer_make_id(&from_inet_address);
				peers_pending_registration_ack[peers_pending_registration_ack_len].given_id = ds_reply.ds_ack_registration.id;
				peers_pending_registration_ack_len ++;

				print_notice("Aggiungo alla lista di attesa di essere registrati il peer con id=%u", ds_reply.ds_ack_registration.id);
			}
			else
				print_warning("Ignoro connessione, il peer %s è già connesso!", sockaddr_in_to_string(&from_inet_address));

			// Invio il pacchetto al peer
			ds_reply.type = TYPE_DS_ACK_REGISTRATION;
			ds_reply.ds_ack_registration.peer_address_as_seen_from_the_outside = peer_make_id(&from_inet_address);

			protocol_p2d_prepare(buffer_out, &ds_reply);
			sendto(fd_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &from_inet_address, sizeof(struct sockaddr_in));
			break;
		case TYPE_PEER_ACK_REGISTRATION:
			// Se questo peer ha una ack pendente la rimuovo e lo aggiungo a quelli connessi
			for(size_t i = 0; ack_index == SIZE_MAX && i < peers_pending_registration_ack_len; i++)
				if (peer_make_id(&from_inet_address) == peers_pending_registration_ack[i].socket_from)
					ack_index = i;

			// Se non l'ò trovato era già connesso ovvero il peer à violato il protocollo
			if(ack_index == SIZE_MAX)
				break;

			// Aggiungo alla lista
			work_node = peer_list_add(&connected_peers, peers_pending_registration_ack[ack_index].given_id, peer_make_id(&from_inet_address));
			work_node->value.last_heartbeat = current_time;
			print_notice("Aggiunto all'indice nuovo peer con id %u e id sessione %lu (%s)", peers_pending_registration_ack[ack_index].given_id, work_node->address_id, sockaddr_in_to_string(&from_inet_address));

			// Sposto tutto a sinistra
			peers_pending_registration_ack_len --;
			memmove(peers_pending_registration_ack + ack_index, peers_pending_registration_ack + ack_index + 1, sizeof(struct peer_SD_assoc)*(peers_pending_registration_ack_len-ack_index));

			// Aggiorno i vicini. In tutto 3 nodi sono coinvolti
			// Dovrò aggiornare il nuovo nodo e i suoi avanti e indietro
			// Si può ridurre un po' la complessità se il numero di peer
			// registrati non è cambiato e i vicini rimangano gli stessi.
			// Si evita di inviare
			for(struct peer_node *i = connected_peers; i; i = i->greater_eq)
				update_neighbors_of_target(connected_peers, i, fd_socket, current_time, next_free_id);

			break;
		case TYPE_PEER_ACK_NEIGHBORS:
			work_node = peer_list_find(connected_peers, peer_make_id(&from_inet_address));

			// Segno come arrivato il pacchetto solo se è l'ack corretto.
			if(work_node && timeval_equal(recived_packet.peer_ack_neighbors.ref_time, work_node->value.last_event))
			{
				work_node->value.last_event.tv_sec = 0;
				work_node->value.last_event.tv_usec = 0;
			}

			if(!work_node)
				print_error("Ricevuto PEER_ACK_NEIGHBORS da %s ma non risulta essere connesso! Ingoro...", sockaddr_in_to_string(&from_inet_address));

			break;
		case TYPE_PEER_SHUTTING_DOWN:
			work_node = peer_list_find(connected_peers, peer_make_id(&from_inet_address));
			if(!work_node)
				break;

			print_notice("Il peer %s si sta disconnettendo dalla rete...", sockaddr_in_to_string(&from_inet_address));
			peer_list_remove(&connected_peers, work_node);

			// Aggiorno i vicini. In tutto 3 nodi sono coinvolti
			// Dovrò aggiornare il nuovo nodo e i suoi avanti e indietro
			// Si può ridurre un po' la complessità se il numero di peer
			// registrati non è cambiato e i vicini rimangano gli stessi.
			// Si evita di inviare
			for(struct peer_node *i = connected_peers; i; i = i->greater_eq)
				update_neighbors_of_target(connected_peers, i, fd_socket, current_time, next_free_id);
			break;
		case TYPE_HEARTBEAT:
			work_node = peer_list_find(connected_peers, peer_make_id(&from_inet_address));
			// Mi segno il battito se last_heartbeat < recived_packet.heartbeat.ref_time
			if(work_node && timeval_subtract(&diff_time, work_node->value.last_heartbeat, recived_packet.heartbeat.ref_time) == 1)
				work_node->value.last_heartbeat = recived_packet.heartbeat.ref_time;

			if(!work_node)
				print_error("Ricevuto TYPE_HEARTBEAT da %s ma non risulta essere connesso! Ingoro...", sockaddr_in_to_string(&from_inet_address));

			break;
		default:
			print_error("Formato pacchetto %lu non riconosciuto ovvero non gesistible da DS!\n", *((uint64_t*)buffer_in));
			break;
		}
	}

	// Invio a tutti i peer il segnale di spegnimento
	protocol_p2d_prepare(buffer_out, &(struct protocol_p2d_packet) {.type = TYPE_DS_SHUTTING_DOWN});
	for(struct peer_node *i = connected_peers; i; i = i->greater_eq)
	{
		struct sockaddr_in t = peer_unmake_id(i->address_id);
		sendto(fd_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &t, sizeof(struct sockaddr_in));

		// Il modo migliore per evitare condizioni di racing!
		nanosleep(&(struct timespec) {.tv_nsec = 50000}, 0);
	}

	peer_list_delete(connected_peers);
	close(fd_socket);
	return 0;
}

static int update_neighbors_of_target(struct peer_node *connected_peers, struct peer_node *target, int fd_socket, struct timeval time, peer_definitive_id max_id)
{
	uint8_t                 buffer_out[PROTOCOL_P2D_PACKET_SIZE];
	//ssize_t                 ret;
	struct peer_node        *neighbors[2] = {0};
	struct sockaddr_in      target_address = peer_unmake_id(target->address_id);
	struct protocol_p2d_packet update_packet = {
		.type = TYPE_DS_UPDATE_NEIGHBORS,
		.ds_update_neighbors.time = time,
		.ds_update_neighbors.current_max_id = max_id - 1
	};

	// Se la lista ha un solo elemento allora invio vicini come lui stesso
	if(connected_peers && !connected_peers->greater_eq && !connected_peers->lesser)
	{
		update_packet.ds_update_neighbors.neighbor_in = 0;
		update_packet.ds_update_neighbors.neighbor_out = 0;
	}
	else
	{
		// Computo i vicini
		peer_list_get_neighbors(connected_peers, target, neighbors);
		update_packet.ds_update_neighbors.neighbor_in = neighbors[0]->address_id;
		update_packet.ds_update_neighbors.neighbor_out = neighbors[1]->address_id;
	}

	protocol_p2d_prepare(buffer_out, &update_packet);

	// Provo a inviare il pacchetto
	sendto(fd_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &target_address, sizeof(struct sockaddr_in));
	//if(ret == -1)
	//	return -1;

	// pacchetto inviato!
	target->value.last_event = time;

	return 0;
}
