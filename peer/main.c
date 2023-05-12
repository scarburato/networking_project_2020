#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include "../common.h"
#include "../shared/protocol.h"
#include "../error.h"
#include "../shared/utility.h"
#include "console_logic.h"
#include "net.h"
#include "disk.h"

struct peer_flooding_status
{
	struct peer_node *pending_flooding_requests;
	time_t query_net_reference;
	unsigned missing_neighbours;
	bool halt_console;
};

struct peer_fds
{
	int fd_udp_socket, fd_tcp_socket;
	int fd_neighbour[2];
	// Descrittori dei socket che osservo per la prontezza in lettura
	fd_set current_fds;
	int max_fd;
	// Descrittore del socket che deve terminare la connect()
	fd_set connect_pending_fds;
};

enum neighbour
{
	PREVIOUS = 0, NEXT = 1
};

/**
 * Logica comune ai due vicini
 * @return
 */
static bool neighbour_common_logic(enum neighbour neighbour, struct control_packet *recived, struct peer_fds *fds, struct peer_flooding_status *cfd, struct disk_status *ds, struct peer_console *pc,
                                   struct ds_connection *dc);

static void test_ring_logic(struct control_packet *recived, struct peer_fds *fds, struct disk_status *ds);

static void neighbour_handle_close(enum neighbour neighbour, struct peer_fds *fds, struct peer_flooding_status *cfd);

static void flood_reply_logic(struct control_packet *recived, struct disk_status *ds, struct ds_connection *dc, struct peer_fds *fds);

/**
 * Apre un canale TCP con recipient_address
 * @return -1 se errore, fd di un socket stream altrimenti
 */
static int open_data_socket(struct sockaddr_in *recipient_address, unsigned long hops);

/**
 * Tempo massimo di attesa sulla select di peer.
 * Massimo 5s 7ms attendo.
 */
static const struct timeval select_timeout = {
	.tv_sec = 5,
	.tv_usec = 7000
};

/**
 * Tempo massimo di attesa sulla connect al destinatario di un flooding
 * Valori vicini o superiori al tempo di timeout del ds (30s) possono
 * causare una disconessione per inattività!
 */
static const struct timeval connect_flooding_recipient_timeout = {
	.tv_sec = 1,
	.tv_usec = 5000
};

static bool terminato = false;

static void my_handler(sig_atomic_t s)
{
	terminato = true;
}

int main(int argc, char **argv)
{
	// Voglio gestire in sincronia gli errori del tubo (es. connessione chiusa senza eof)
	signal(SIGPIPE, SIG_IGN);
	// ^C
	signal (SIGINT,my_handler);

	int ret;

	/** Variabili di stato del peer */
	//struct timeval current_time;
	struct peer_console console_status = {0};
	;

	/** L'Indirizzo del Discovery Server */
	struct ds_connection connection = {0};
	/** Il mio indirizzo di ascolto. */
	struct sockaddr_in listen_address = {
		.sin_family = AF_INET,
		// Se non mi passa nulla, lascio scegliere al S.O. la porta
		.sin_port = htons(argc >= 2 ? atoi(argv[1]) : 0),
		.sin_addr.s_addr = INADDR_ANY
	};
	struct sockaddr_in listen_address_tmp;
	socklen_t listen_address_size = sizeof(struct sockaddr_in);
	/** Gli indirizzi dei miei vicini
	 * - neighbour[PREVIOUS]: Il vicino che dovrò fare la accept()
	 * - neighbour[NEXT]: Il vicino che dovrò fare la connect() */
	struct sockaddr_in neighbour[2] = {{0}};

	/** Roba di IO I/O*/
	uint8_t buffer_out[PROTOCOL_P2D_PACKET_SIZE];
	uint8_t buffer_in[PROTOCOL_P2D_PACKET_SIZE];
	struct peer_fds fds = {
		.fd_neighbour[PREVIOUS] = -1,
		.fd_neighbour[NEXT] = -1
	};
	struct timeval select_elapsed_time;

	/** Varibili di stato della memoria del peer */
	struct disk_status my_disk_status = disk_init(time(0));

	/** Richieste di flooding che mancano all'appello per terminare una query */
	struct peer_flooding_status cfd = {0};

	// Creo i due socket udp e tcp
	fds.fd_udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (fds.fd_udp_socket == -1)
		exit_throw("Creazione del socket UDP", errno);

	fds.fd_tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (fds.fd_tcp_socket == -1)
		exit_throw("Creazione del socket TCP", errno);

	// Collego i due socket alla porta
	ret = bind(fds.fd_udp_socket, (struct sockaddr *) &listen_address, sizeof(struct sockaddr_in));
	if (ret == -1)
		exit_throw("Bind del socket UDP sull'indirizzo", errno);

	// Questo lo faccio solo per ottenere la mia porta se non ho specificato nelle opzioni su quale porta
	ret = getsockname(fds.fd_udp_socket, (struct sockaddr *) &listen_address_tmp, &listen_address_size);
	if (ret == -1)
		exit_throw("getsockname() su socket udp", errno);

	listen_address.sin_port = listen_address_tmp.sin_port;
	print_notice("Mi è stata assegnata porta %u", ntohs(listen_address.sin_port));

	// Prima di fare la bind
	setsockopt(fds.fd_tcp_socket, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));

	ret = bind(fds.fd_tcp_socket, (struct sockaddr *) &listen_address, sizeof(struct sockaddr_in));
	if (ret == -1)
		exit_throw("Bind del socket TCP sull'indirizzo", errno);

	ret = listen(fds.fd_tcp_socket, 5);
	if (ret == -1)
		exit_throw("listen() sul socket tcp", errno);

	// Aggiungo stdin e i due fd tcp e udp a current_fds. Quanto mi connetterò aggiungero
	// anche i miei vicini
	FD_SET(STDIN_FILENO, &fds.current_fds);
	FD_SET(fds.fd_udp_socket, &fds.current_fds);
	FD_SET(fds.fd_tcp_socket, &fds.current_fds);
	fds.max_fd = max(max(STDIN_FILENO, fds.fd_udp_socket), fds.fd_tcp_socket);
	//printf("%d, %d, %d -> %d\n", STDIN_FILENO, fd_udp_socket, fd_tcp_socket, max_fd);

	// Auto-boot
	if (argc > 2)
	{
		print_notice("Richiesto autoboot!%s", "");
		connection = connect_to_ds(fds.fd_udp_socket, &(struct sockaddr_in) {
			.sin_family = AF_INET,
			.sin_port = htons(7575),
			.sin_addr.s_addr = htonl(INADDR_LOOPBACK)
		}, my_disk_status.my_id);
		if (!connection.connected)
			exit_throw("Auto-boot fallito", ENETDOWN);

		if (!peers_register_set_my_id(&my_disk_status, connection.my_id))
			exit_throw("Errore registrazione del mio id nel registro", errno);
		print_notice("Connesso! Il mio id è %u", connection.my_id);
	}

	while (!terminato)
	{
		fd_set fds_ready_to_read = fds.current_fds;
		fd_set fds_ready_to_write = fds.connect_pending_fds;
		select_elapsed_time = select_timeout;

		// Prompt della console prima di tutto
		if (!cfd.halt_console)
			peer_console_show(&console_status, &connection);

		ret = select(fds.max_fd + 1, &fds_ready_to_read, &fds_ready_to_write, 0, &select_elapsed_time);
		if (ret == -1 && errno != EINTR)
			exit_throw("Errore sulla select()", errno);

		if (ret == -1 && errno == EINTR)
			continue;

		/***************************************************************
		*               LOGICA DEL SOCKET TCP
		*****************************************************************/
		if (FD_ISSET(fds.fd_tcp_socket, &fds_ready_to_read))
		{
			struct sockaddr_in from;
			int fd_accepted_socket;
			socklen_t sockaddr_in_len = sizeof(struct sockaddr_in);
			enum peer_connection_type connection_type = 0;
			uint8_t raw_byte;

			fd_accepted_socket = accept(fds.fd_tcp_socket, (struct sockaddr *) &from, &sockaddr_in_len);
			if (fd_accepted_socket == -1)
				exit_throw("accept() dal vicino/fornitore dei dati", errno);

			print_notice("Connessione in ingresso da %s", sockaddr_in_to_string(&from));

			// @fixme: Condizione di racing! Possibile blocco
			ret = recv(fd_accepted_socket, &raw_byte, 1, MSG_WAITALL);
			if (ret == -1)
				exit_throw("recv() da connessione tcp", errno);

			connection_type = raw_byte;

			// Connessione di controllo. Dovrebbe essere il vicino "sinistro".
			if (connection_type == CONTROL)
			{
				if (fds.fd_neighbour[PREVIOUS] != -1)
				{
					close(fds.fd_neighbour[PREVIOUS]);
					FD_CLR(fds.fd_neighbour[PREVIOUS], &fds.current_fds);
				}
				fds.fd_neighbour[PREVIOUS] = fd_accepted_socket;

				// Aggiungo il vicino al gruppo dei descrittori
				FD_SET(fds.fd_neighbour[PREVIOUS], &fds.current_fds);
				fds.max_fd = max(fds.max_fd, fds.fd_neighbour[PREVIOUS]);

				goto leave_tcp_if;
			}

			if (connection_type != DATA)
			{
				print_error("Tipo di connessione %u non valida!", connection_type);
				close(fd_accepted_socket);
				goto leave_tcp_if;
			}

			print_notice("È di tipo DATA%s", "");
			// Chiama recive_data
			recive_data(fd_accepted_socket, &my_disk_status);

			close(fd_accepted_socket);
		}
		leave_tcp_if:

		/**************************************************************
		 * 		TERMINATA ACCEPT DEL VICINO NEXT?
		 ***************************************************************/
		if (fds.fd_neighbour[NEXT] != -1 && FD_ISSET(fds.fd_neighbour[NEXT], &fds_ready_to_write))
		{
			int error;
			socklen_t error_len = sizeof(int);

			ret = getsockopt(fds.fd_neighbour[NEXT], SOL_SOCKET, SO_ERROR, &error, &error_len);
			if (ret == -1)
				exit_throw("getsockopt()", errno);
			if (error == ECONNREFUSED)
			{
				print_warning("Il vicino ha già cambiato vicino?! Ricevuto errore %d", ret);
				close(fds.fd_neighbour[NEXT]);
				FD_CLR(fds.fd_neighbour[NEXT], &fds.current_fds);
				fds.fd_neighbour[NEXT] = -1;
				goto leave_accept_if;
			}
			if (error)
				exit_throw("connect() fallita. il sockopt SO_ERROR != 0", error);

			print_notice("Il vicino ha accettato la connessione!%s", "");

			// Connesso. aggiungo il descrittori a quelli da osservera in lettura dalla select()
			FD_SET(fds.fd_neighbour[NEXT], &fds.current_fds);

			// Non c'è più bisogno di averlo qua
			FD_ZERO(&fds.connect_pending_fds);

			// Invio il tipo di socket
			// Ovviamente, sono un canale di CONTROL non DATA
			enum peer_connection_type connection_type = CONTROL;
			ret = send(fds.fd_neighbour[NEXT], &connection_type, 1, 0);
			// se ho un broken pipe vuol dire che il vicino ha già cambiato vicino
			if (ret == -1 && errno != EWOULDBLOCK && errno != EPIPE && errno != ECONNRESET)
				exit_throw("Invio tipo di conessione `CONTROL' fallito", errno)

			if (ret < 1)
			{
				print_warning("Il vicino ha già cambiato vicino?! Ricevuto errore %d", ret);
				close(fds.fd_neighbour[NEXT]);
				FD_CLR(fds.fd_neighbour[NEXT], &fds.current_fds);
				fds.fd_neighbour[NEXT] = -1;
			} else
			{
				// Prima di continuare imposto il socket come bloccante
				ret = fcntl(fds.fd_neighbour[NEXT], F_GETFL, 0);
				if (ret == -1)
					exit_throw("fcntl()", errno);

				ret = ret & ~O_NONBLOCK;
				ret = fcntl(fds.fd_neighbour[NEXT], F_SETFL, ret);
				if (ret == -1)
					exit_throw("fcntl()", errno);
			}
		}
		leave_accept_if:

		/***********************************************************************
		 * 			LOGICA DEI VICINI
		 ***********************************************************************/
		if (fds.fd_neighbour[PREVIOUS] != -1 && FD_ISSET(fds.fd_neighbour[PREVIOUS], &fds_ready_to_read))
		{
			uint8_t buffer_control[CONTROL_PACKET_SIZE] = {0};
			struct control_packet recived;

			ret = recv(fds.fd_neighbour[PREVIOUS], buffer_control, CONTROL_PACKET_SIZE, MSG_WAITALL);
			if (ret == 0 || (ret == -1 && errno == ECONNRESET))
			{
				neighbour_handle_close(PREVIOUS, &fds, &cfd);
				// Nulla da fare qua, esco dall'IF
				goto leave_fd_previous_peer_if;
			}
			if (ret == -1)
				exit_throw("recv()", errno);

			recived = protocol_control_parse(buffer_control);
			ret = neighbour_common_logic(PREVIOUS, &recived, &fds, &cfd, &my_disk_status, &console_status, &connection);
			if (ret)
				goto leave_fd_previous_peer_if;

			switch (recived.type)
			{
			case TYPE_ASK_FOR_ENTRIES:
				// È una richiesta mia che à fatto il giro!
				if (recived.data.ask_for_entries.recipient == peer_make_id(&connection.my_address_from_the_ouside))
				{
					// Ma è vecchia. Allora la scarto.
					if (recived.data.ask_for_entries.flood_ref != cfd.query_net_reference)
					{
						print_warning("È tornata la richiesta flooding %lu ma è scaduta! Scartata.", recived.data.ask_for_entries.flood_ref);
						break;
					}

					struct peer_node *target = peer_list_find(cfd.pending_flooding_requests, recived.data.ask_for_entries.target);
					if (!target)
					{
						print_error("È arrivata una richiesta di flooding mia ma il target %u non era in lista 🤔", recived.data.ask_for_entries.target);
						break;
					}

					// 0k è in lista: lo rimuovo dalla lista pendenti. Se la lista = ø => posso ri fare il computo query
					print_query_log("La richiesta %lu per autore %hu è tornata!", recived.data.ask_for_entries.flood_ref, recived.data.ask_for_entries.target);
					peer_list_remove(&cfd.pending_flooding_requests, target);
					if (cfd.pending_flooding_requests)
						break;

					print_query_log("Flooding completato. A momenti terminerò il computo%s", "");
					struct query_result r = query_compute(console_status.q, &my_disk_status, &connection, -1, fds.fd_neighbour[NEXT], &cfd.pending_flooding_requests,
					                                      QUERY_COMPUTE_SKIP_CACHE | QUERY_COMPUTE_SKIP_NEIGHBOURS | QUERY_COMPUTE_SKIP_FLOODING);
					if (r.status != SUCCESS)
						print_error("Il risultato non dovrebbe avere status = %u ora!", r.status);

					dprintqueryresult(STDOUT_FILENO, &r);

					// Ora posso ricominciare ad ascolatare sulla telescrivente
					FD_SET(STDIN_FILENO, &fds.current_fds);
					cfd.halt_console = 0;
					break;
				}

				// 0k, ora posso rispondere
				if (recived.data.ask_for_entries.hops >= MAX_HOPS)
					print_error("Pacchetto di flooding à fatto troppi salti, probabile loop(?). Lo fermo!%s", "");
				else
					flood_reply_logic(&recived, &my_disk_status, &connection, &fds);
				break;
			case TYPE_TEST_RING:
				test_ring_logic(&recived, &fds, &my_disk_status);
				break;
			default:
				print_error("Pacchetto invalido %u è stato scartato", recived.type);
				break;
			}
		}
		leave_fd_previous_peer_if:

		/**
		 * Logica del vicino uscente. Da questo vicino non mi aspetto i pacchetti a "senso unico"
		 * che sono inviati dalla mia connessione TCP a lui e non viceversa.
		 */
		if (fds.fd_neighbour[NEXT] != -1 && FD_ISSET(fds.fd_neighbour[NEXT], &fds_ready_to_read))
		{
			uint8_t buffer_control[CONTROL_PACKET_SIZE] = {0};
			struct control_packet recived;

			ret = recv(fds.fd_neighbour[NEXT], buffer_control, CONTROL_PACKET_SIZE, MSG_WAITALL);
			if (ret == 0 || (ret == -1 && errno == ECONNRESET))
			{
				neighbour_handle_close(NEXT, &fds, &cfd);
				// Nulla da fare qua, esco dall'IF
				goto leave_fd_next_peer_if;
			}
			if (ret == -1)
				exit_throw("recv()", errno);

			recived = protocol_control_parse(buffer_control);
			ret = neighbour_common_logic(NEXT, &recived, &fds, &cfd, &my_disk_status, &console_status, &connection);
			if (!ret)
				print_error("Pacchetto non gestibile arrivato da vicino anteriore. type=%u", recived.type);
		}
		leave_fd_next_peer_if:

		/***************************************************************
		*               LOGICA DEL SOCKET UDP
		*****************************************************************/
		if (FD_ISSET(fds.fd_udp_socket, &fds_ready_to_read))
		{
			struct sockaddr_in from_address;
			socklen_t from_address_len = sizeof(struct sockaddr_in);
			struct protocol_p2d_packet packet_in;
			peer_session_id n_in_ser_old;

			ret = recvfrom(fds.fd_udp_socket, buffer_in, PROTOCOL_P2D_PACKET_SIZE, 0, (struct sockaddr *) &from_address, &from_address_len);
			if (ret == -1)
				exit_throw("Errore sulla recvfrom() da socket udp", errno);

			// Ignoro pacchetto non dal ds
			if (!connection.connected || !sockaddr_in_equal(&from_address, &connection.ds_address))
			{
				print_warning("Ignoro pacchetto udp da %s, perché non arriva dal ds", sockaddr_in_to_string(&from_address));
				goto leave_udp_if;
			}

			// Parse del pacchetto
			packet_in = protocol_p2d_parse(buffer_in);
			ret = 0;
			switch (packet_in.type)
			{
				// Il DS continua a mandare acks (forse qualche pezzo di handshake è andato perso...)
			case TYPE_DS_ACK_REGISTRATION:
				protocol_p2d_prepare(buffer_out, &(struct protocol_p2d_packet) {
					.type = TYPE_PEER_ACK_REGISTRATION
				});
				ret = sendto(fds.fd_udp_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &connection.ds_address, sizeof(struct sockaddr_in));
				break;
			case TYPE_DS_UPDATE_NEIGHBORS:
				// Invio la ACK al ds prima di proseguire
				protocol_p2d_prepare(buffer_out, &(struct protocol_p2d_packet) {
					.type = TYPE_PEER_ACK_NEIGHBORS,
					.peer_ack_neighbors.ref_time = packet_in.ds_update_neighbors.time
				});
				ret = sendto(fds.fd_udp_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &connection.ds_address, sizeof(struct sockaddr_in));

				// Nuovo massimo
				connection.max_id = packet_in.ds_update_neighbors.current_max_id;

				// Salvo il vecchio vicino uscente, per evitare disconessioni inutili
				n_in_ser_old = peer_make_id(neighbour + NEXT);

				neighbour[PREVIOUS] = peer_unmake_id(packet_in.ds_update_neighbors.neighbor_in);
				neighbour[NEXT] = peer_unmake_id(packet_in.ds_update_neighbors.neighbor_out);

				print_notice("Ho ricevuto nuovo vicino entrante\t%s", sockaddr_in_to_string(neighbour + PREVIOUS));
				print_notice("Ho ricevuto nuovo vicino uscente\t%s", sockaddr_in_to_string(neighbour + NEXT));

				// Mi provo a connettere al nodo uscente
				// Naturalmente se 1. esiste 2. non vi sono già connesso
				if (packet_in.ds_update_neighbors.neighbor_out && packet_in.ds_update_neighbors.neighbor_out != n_in_ser_old)
				{
					print_notice("Mi collegerò al mio nuovo vicino\t%s!", sockaddr_in_to_string(neighbour + NEXT));
					// Per comodità ricreo il socket anziché riutilizzarlo
					if (fds.fd_neighbour[NEXT] != -1)
					{
						close(fds.fd_neighbour[NEXT]);
						FD_CLR(fds.fd_neighbour[NEXT], &fds.current_fds);
					}
					FD_ZERO(&fds.connect_pending_fds);

					fds.fd_neighbour[NEXT] = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
					if (fds.fd_neighbour[NEXT] == -1)
						exit_throw("socket() del socket successivo", errno);

					// Mi provo a connettere al mio vicini
					ret = connect(fds.fd_neighbour[NEXT], (const struct sockaddr *) neighbour + NEXT, sizeof(struct sockaddr_in));
					if (ret == -1 && errno != EWOULDBLOCK && errno != EINPROGRESS)
						exit_throw("connect() del vicino", errno);

					// Quando la connect() terminarà (con errore ovvero con successo)
					// il socket sarà pronto per la scrittura!
					FD_SET(fds.fd_neighbour[NEXT], &fds.connect_pending_fds);
					fds.max_fd = max(fds.max_fd, fds.fd_neighbour[NEXT]);

					ret = 0;
				}
				break;
			case TYPE_HEARTBEAT:
				protocol_p2d_prepare(buffer_out, &(struct protocol_p2d_packet) {
					.type = TYPE_HEARTBEAT,
					.heartbeat.ref_time = packet_in.heartbeat.ref_time
				});
				ret = sendto(fds.fd_udp_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &connection.ds_address, sizeof(struct sockaddr_in));
				break;
			case TYPE_DS_SHUTTING_DOWN:
				print_notice("Il ds ha inviato il segnale di arresto. Disconessione in corso. %u", 0);
				terminato = true;
				//memset(&connection, 0x00, sizeof(struct ds_connection));
				break;
			case TYPE_INVALID:
				print_errno("Ignoro pacchetto da ds invalido", packet_in.error);
				break;
			default:
				print_error("Ignoro pacchetto di tipo non riconosciuto %u", packet_in.type);
				break;
			}

			if (ret == -1)
				exit_throw("sendto() in risposta al ds fallita", errno);
		}
		leave_udp_if:

		/***************************************************************
		 *               LOGICA DELLA CONSOLE
		 *****************************************************************/
		if (!cfd.halt_console && FD_ISSET(STDIN_FILENO, &fds_ready_to_read))
		{
			struct query_result r;
			uint8_t buffer_control[CONTROL_PACKET_SIZE];
			enum peer_console_command command = peer_console_dispatcher(&console_status);
			//int vicini[2] = {fd_neighbour[NEXT], fd_neighbour[PREVIOUS]};

			switch (command)
			{
			case CONSOLE_QUIT:
				print_notice("È stato richiesta la chiusura. %u", fds.fd_udp_socket);
				terminato = true;
				continue;
				break;
			case CONSOLE_COMMAND_START:
				connection = connect_to_ds(fds.fd_udp_socket, &console_status.ds_address, my_disk_status.my_id);
				if (!connection.connected)
					break;

				if (!peers_register_set_my_id(&my_disk_status, connection.my_id))
					exit_throw("Errore registrazione del mio id nel registro", errno);
				print_notice("Connesso! Il mio id è %u", connection.my_id);
				break;
			case CONSOLE_COMMAND_ADD:
				if (!connection.connected)
				{
					print_error("Connesione, non presente. Non posso fare niente... %i", connection.connected);
					break;
				}

				console_status.entry.author = my_disk_status.my_id;
				ret = data_register_add_entry(&my_disk_status, &console_status.entry);
				if (ret != 0)
					exit_throw("data_register_add_entry()", ret);
				break;
			case CONSOLE_COMMAND_GET:
				if (!connection.connected)
				{
					print_error("Connesione, non presente. Non posso fare niente... %i", connection.connected);
					break;
				}

				r = query_compute(console_status.q, &my_disk_status, &connection, fds.fd_neighbour[PREVIOUS], fds.fd_neighbour[NEXT], &cfd.pending_flooding_requests,
				                  QUERY_COMPUTE_SKIP_FLOODING);
				if (r.status == MISSING_ENTIRES_ASK_NET_CACHE)
				{
					print_query_log("Sto chiedendo alla cache dei vicini. La console si bloccherà!%s", "");

					// Mi salvo il riferimento dell'inizio
					cfd.query_net_reference = r.reference;
					cfd.missing_neighbours = 0b11;

					// Blocco la console
					FD_CLR(STDIN_FILENO, &fds.current_fds);
					cfd.halt_console = 1;
					break;
				}

				if (console_status.q.aggr == TOTAL)
					dprintqueryresult(STDOUT_FILENO, &r);
				break;
			case CONSOLE_COMMAND_SHOW:
				for (size_t i = 0; i < my_disk_status.registers_length; i++)
				{
					struct data_register r = open_data_register(&my_disk_status, my_disk_status.registers[i]);
					char tmp[50] = {0};
					strftime(tmp, 50, "%Y-%m-%d", gmtime(my_disk_status.registers + i));
					printf("========== %s ==========\n", tmp);

					for (size_t i = 0; i < r.length; i++)
					{
						strftime(tmp, 50, "%Y-%m-%dT%H:%M:%SZ", gmtime(&r.entries[i].date));
						printf(
							"» %s\t%hu\t%s\t%hu\n",
							tmp, r.entries[i].author, r.entries[i].type == SWAB ? "tampone" : "caso", r.entries[i].quantity
						);
					}
				}
				break;
			case CONSOLE_COMMAND_SHOW_STATE:
				for (peer_definitive_id i = 1; i <= connection.max_id; i++)
				{
					time_t lu = peers_register_get_last_update(&my_disk_status, i);
					char tmp[50] = {0};
					strftime(tmp, 50, "%Y-%m-%dT%H:%M:%SZ", gmtime(&lu));
					printf("» %hu\t%s\n", i , tmp);
				}
				break;
			case CONSOLE_COMMAND_TEST:
				if (fds.fd_neighbour[NEXT] == -1)
				{
					print_warning("Test su rete di un singolo peer vietata!%s", "");
					break;
				}

				protocol_control_prepare_packet(buffer_control, &(struct control_packet) {
					.type = TYPE_TEST_RING,
					.data.test.sender = my_disk_status.my_id
				});
				ret = send(fds.fd_neighbour[NEXT], buffer_control, CONTROL_PACKET_SIZE, 0);
				if (ret == -1)
					exit_throw("send()", errno);
				break;
			default:
				break;
			}
		}
	}

	/**
	 * Procedura di chiusura: prima di continuare chiedo ai miei vicini
	 * di mandarmi il loro margine temporale più basso, dopo di che
	 * invierò tutte le mie voci di registro aggiorante da loro margine
	 * al mio margine più alto
	 */
	print_notice("Tentativo di disconessione gentile dalla rete, i vicini hanno 2.5 secondi per rispondere%s", "");
	terminato = 0;
	select_elapsed_time = (struct timeval) {.tv_sec = 2, .tv_usec = 5000};
	FD_ZERO(&fds.current_fds);
	FD_SET(fds.fd_tcp_socket, &fds.current_fds);
	if (fds.fd_neighbour[NEXT] != -1)
		FD_SET(fds.fd_neighbour[NEXT], &fds.current_fds);
	if (fds.fd_neighbour[PREVIOUS] != -1)
		FD_SET(fds.fd_neighbour[PREVIOUS], &fds.current_fds);
	if (fds.fd_neighbour[PREVIOUS] != -1 || fds.fd_neighbour[NEXT] != -1)
		fds.max_fd = max(fds.fd_neighbour[NEXT], fds.fd_neighbour[PREVIOUS]);
	else
		fds.max_fd = -1;

	/**
	 * Chiedo i lower bound dei miei vicini così che possa
	 * inviare i miei registri a loro
	 */
	for (enum neighbour n = PREVIOUS; n <= NEXT; n++)
	{
		if (fds.fd_neighbour[n] == -1)
			continue;

		uint8_t buffer[CONTROL_PACKET_SIZE];
		protocol_control_prepare_packet(buffer, &(struct control_packet) {
			.type = TYPE_ASK_FOR_LOWEST_LOWER_BOUND,
		});

		ret = send(fds.fd_neighbour[n], buffer, CONTROL_PACKET_SIZE, 0);
		if (ret == -1)
			exit_throw("send()", errno);
	}

	while (fds.max_fd != -1 && !terminato && !timeval_equal(select_elapsed_time, (struct timeval) {0}))
	{
		fd_set ready_to_read_fds = fds.current_fds;
		ret = select(fds.max_fd + 1, &ready_to_read_fds, 0, 0, &select_elapsed_time);

		/** Conessione in ingresso, adesso accetto solo DATA */
		if (FD_ISSET(fds.fd_tcp_socket, &ready_to_read_fds))
		{
			struct sockaddr_in from;
			int fd_accepted_socket;
			socklen_t sockaddr_in_len = sizeof(struct sockaddr_in);
			enum peer_connection_type connection_type = 0;
			uint8_t raw_byte;

			fd_accepted_socket = accept(fds.fd_tcp_socket, (struct sockaddr *) &from, &sockaddr_in_len);
			if (fd_accepted_socket == -1)
				exit_throw("accept() dal vicino/fornitore dei dati", errno);

			print_notice("Connessione in ingresso da %s", sockaddr_in_to_string(&from));

			// @fixme: uno che non rispetta il protocollo farà bloccare il programma qua!
			ret = recv(fd_accepted_socket, &raw_byte, 1, MSG_WAITALL);
			if (ret == -1)
				exit_throw("recv() da connessione tcp", errno);

			connection_type = raw_byte;
			if (connection_type != DATA)
				print_warning("Ignoro questo tipo di conessione %u. Nel poweroff accetto solo DATA", connection_type);
			else
				recive_data(fd_accepted_socket, &my_disk_status);

			close(fd_accepted_socket);
		}

		/** Logica dei due vicini NEXT, PREVIOUS */
		for (enum neighbour n = PREVIOUS; n <= NEXT; n++)
		{
			if (fds.fd_neighbour[n] == -1 || !FD_ISSET(fds.fd_neighbour[n], &ready_to_read_fds))
				continue;

			uint8_t packet[CONTROL_PACKET_SIZE];
			ret = recv(fds.fd_neighbour[n], packet, CONTROL_PACKET_SIZE, MSG_WAITALL);
			if (ret == 0 || (ret == -1 && errno == ECONNRESET))
			{
				neighbour_handle_close(n, &fds, &cfd);
				continue;
			}
			if (ret == -1)
				exit_throw("recv()", errno);

			struct control_packet recived = protocol_control_parse(packet);
			neighbour_common_logic(n, &recived, &fds, &cfd, &my_disk_status, &console_status, &connection);
			int fd_data_socket;

			// Pacchetti speciali
			switch (recived.type)
			{
				// Aggiornamento al vicino. terminato l'aggiornamento chiudo la conessione col vicino
			case TYPE_REPLY_LOWEST_LOWER_BOUND:
				fd_data_socket = open_data_socket(&neighbour[n], 0);
				if (fd_data_socket == -1)
					break;

				transmit_data(fd_data_socket, &my_disk_status, &connection, 0, recived.data.reply_lowest_lower_bound.lower_bound, time(0));
				neighbour_handle_close(n, &fds, &cfd);
				break;
				// Pacchetti che devo comununque gestire
			case TYPE_TEST_RING:
				test_ring_logic(&recived, &fds, &my_disk_status);
				break;
			case TYPE_ASK_FOR_ENTRIES:
				// È una richiesta mia che à fatto il giro! Oramai è troppo tardi per queste cose...
				if (recived.data.ask_for_entries.recipient == peer_make_id(&connection.my_address_from_the_ouside))
					break;

				flood_reply_logic(&recived, &my_disk_status, &connection, &fds);
				break;
			default:
				break;
			}
		}

		terminato = fds.fd_neighbour[PREVIOUS] == -1 && fds.fd_neighbour[NEXT] == -1;
	}

	if (fds.fd_neighbour[PREVIOUS] != -1 || fds.fd_neighbour[NEXT] != -1)
		print_warning("Disconessione gentile fallita (prev=%d, next=%d). Il db potrebbe non essere consistente", fds.fd_neighbour[PREVIOUS], fds.fd_neighbour[NEXT]);
	else
		print_notice("Successo!%s", "");

	// Se sono collegatoa un DS, gli invio il segnale di arresto
	// Non mi preoccupo più tanto del suo arrivo perché dopo un
	// certo periodo di inattività mi disconetterà automaticamente
	// in ogni caso.
	if (connection.connected)
	{
		protocol_p2d_prepare(buffer_out, &(struct protocol_p2d_packet) {
			.type = TYPE_PEER_SHUTTING_DOWN
		});
		ret = sendto(fds.fd_udp_socket, buffer_out, PROTOCOL_P2D_PACKET_SIZE, 0, (const struct sockaddr *) &connection.ds_address, sizeof(struct sockaddr_in));
		if (ret == -1)
			print_errno("sendto() del segnale d'arresto al ds fallita...", errno);
	}

	close(fds.fd_tcp_socket);
	close(fds.fd_udp_socket);

	if (fds.fd_neighbour[NEXT] != -1)
		close(fds.fd_neighbour[NEXT]);
	if (fds.fd_neighbour[PREVIOUS] != -1)
		close(fds.fd_neighbour[PREVIOUS]);

	disk_exit(&my_disk_status);
	return 0;
}

static bool neighbour_common_logic(enum neighbour neighbour, struct control_packet *recived, struct peer_fds *fds, struct peer_flooding_status *cfd, struct disk_status *ds, struct peer_console *pc,
                                   struct ds_connection *dc)
{
	uint8_t buffer_control[CONTROL_PACKET_SIZE];
	int ret;

	switch (recived->type)
	{
	case TYPE_SEND_QUERY:
		protocol_control_prepare_packet(buffer_control, &(struct control_packet) {
			.type = TYPE_SEND_QUERY_RESULT,
			.data.send_query_result = query_find_in_cache(recived->data.send_query),
		});
		ret = send(fds->fd_neighbour[neighbour], &buffer_control, CONTROL_PACKET_SIZE, 0);
		if (ret == -1)
			exit_throw("ret()", errno);
		break;
	case TYPE_ASK_FOR_LOWEST_LOWER_BOUND:
		protocol_control_prepare_packet(buffer_control, &(struct control_packet) {
			.type = TYPE_REPLY_LOWEST_LOWER_BOUND,
			.data.reply_lowest_lower_bound.lower_bound = peers_register_get_lowest_last_update(ds, dc->max_id)
		});
		ret = send(fds->fd_neighbour[neighbour], &buffer_control, CONTROL_PACKET_SIZE, 0);
		if (ret == -1)
			exit_throw("ret()", errno);
		break;
	case TYPE_SEND_QUERY_RESULT:
		// Riferimento sbagliato, ignoro. Forse richiesta vecchia?
		if (!cfd->missing_neighbours /*|| recived.data.send_query_result.reference != query_net_reference*/)
			break;

		// Tolgo il mio bit
		cfd->missing_neighbours = cfd->missing_neighbours & (neighbour == NEXT ? 0b01 : 0b10);

		// Non lo aveva in cache.
		if (!cfd->missing_neighbours && recived->data.send_query_result.status == INVALID_RESULT)
		{
			// Ò finito i vicini, È tempo di inizare il floodinf
			struct query_result r = query_compute(pc->q, ds, dc, fds->fd_neighbour[PREVIOUS], fds->fd_neighbour[NEXT], &cfd->pending_flooding_requests,
			                                      QUERY_COMPUTE_SKIP_NEIGHBOURS | QUERY_COMPUTE_SKIP_CACHE);

			if (r.status == MISSING_ENTRIES_FLOOD)
			{
				print_query_log("Cache vicini fallita, inizia il flooding%s", "");

				// Mi salvo il riferimento dell'inizio
				cfd->query_net_reference = r.reference;
				break;
			}

			if (r.status == SUCCESS)
				dprintqueryresult(STDOUT_FILENO, &r);
			else
				print_error("Query ovvero flooding fallito ritorno alla console! r.status = %u", r.status);

		}
		else if(recived->data.send_query_result.status == SUCCESS )
		{
			print_query_log("Trovato nella cache del vicino!%s", "");
			dprintqueryresult(STDOUT_FILENO, &recived->data.send_query_result);
		}


		// Libero la console
		if(!cfd->missing_neighbours)
		{
			cfd->missing_neighbours = 0;
			cfd->halt_console = 0;
			FD_SET(STDIN_FILENO, &fds->current_fds);
		}
		break;
	default: // Pacchetto non gesibile in comune
		return 0;
	}

	return 1;
}

static void test_ring_logic(struct control_packet *recived, struct peer_fds *fds, struct disk_status *ds)
{
	uint8_t buffer_control[CONTROL_PACKET_SIZE];

	if (recived->data.test.sender == ds->my_id)
	{
		print_notice("»»»»»»»»» Ò RICEVUTO IL MIO TOKEN INDIETRO! hop = %lu", recived->data.test.hops);
		return;
	}

	if (fds->fd_neighbour[NEXT] == -1)
	{
		print_warning("»»»»»»»»» Loop corrotto? Non ho nessun a cui passarlo!%s", "");
		return;
	}

	print_notice("»»»»»»»»» Pacchetto di test in transito hop=%lu id=%u", recived->data.test.hops, recived->data.test.sender);

	if (recived->data.test.hops >= MAX_HOPS)
	{
		print_warning("»»»»»»»»» Pacchetto di test à fatto troppi salti, probabile loop(?). Lo fermo!%s", "");
		return;
	}

	recived->data.test.hops++;
	protocol_control_prepare_packet(buffer_control, recived);
	int ret = send(fds->fd_neighbour[NEXT], buffer_control, CONTROL_PACKET_SIZE, 0);
	if (ret == -1)
		exit_throw("send()", errno);
}

static void neighbour_handle_close(enum neighbour neighbour, struct peer_fds *fds, struct peer_flooding_status *cfd)
{
	int my_bit = neighbour == NEXT ? 0b01 : 0b10;
	int other_bit = neighbour != NEXT ? 0b01 : 0b10;;

	print_warning("Chiusura conessione da vicino %s", neighbour == NEXT ? "uscente" : "entrante");
	close(fds->fd_neighbour[neighbour]);
	FD_CLR(fds->fd_neighbour[neighbour], &fds->current_fds);
	fds->fd_neighbour[neighbour] = -1;

	if (cfd->missing_neighbours & other_bit)
	{
		// Tolgo il mio bit
		cfd->missing_neighbours = cfd->missing_neighbours & my_bit;
	}
	// Se c'era qualcosa in sospeso lo sblocco
	if (!cfd->missing_neighbours)
	{
		cfd->halt_console = 0;
		FD_SET(STDIN_FILENO, &fds->current_fds);
	}
}

static void flood_reply_logic(struct control_packet *recived, struct disk_status *ds, struct ds_connection *dc, struct peer_fds *fds)
{
	uint8_t buffer_control[CONTROL_PACKET_SIZE];
	// Controllo se ho i dati richiesti. Cioè se il mio last update è anteriore al suo limite inferiore
	time_t last_update = peers_register_get_last_update(ds, recived->data.ask_for_entries.target);
	if (last_update > recived->data.ask_for_entries.lower_bound)
	{
		struct sockaddr_in recipient_address = peer_unmake_id(recived->data.ask_for_entries.recipient);

		print_query_log(
			"In ingresso richiesta di flooding target=%hu\trecipient=%s\tlow=%lu\tup=%lu\tmio_last_update=%lu",
			recived->data.ask_for_entries.target,
			sockaddr_in_to_string(&recipient_address),
			recived->data.ask_for_entries.lower_bound,
			recived->data.ask_for_entries.upper_bound,
			last_update
		);

		int fd_data_stream = open_data_socket(&recipient_address, recived->data.ask_for_entries.hops);
		if (fd_data_stream == -1)
			return;

		bool pipe_broken = !transmit_data(fd_data_stream, ds, dc, recived->data.ask_for_entries.target, recived->data.ask_for_entries.lower_bound, min(last_update, recived->data.ask_for_entries.upper_bound));
		close(fd_data_stream);

		// → 5. Modifico la richiesta coi nuovi margini temporali
		// Se non sono riuscito a completarla magari ci riuscirà un mio successore
		if (pipe_broken)
			print_error("Tubo rotto %hu?", pipe_broken);
		recived->data.ask_for_entries.lower_bound = min(last_update, recived->data.ask_for_entries.upper_bound);
	} else
		print_query_log("In ingresso richiesta di flooding target=%hu. PASSATA AVANTI NON MODIFICATA", recived->data.ask_for_entries.target);

	//[→6.] Invio al mio vicino la richiesta modificata
	protocol_control_prepare_packet(buffer_control, recived);
	int ret = send(fds->fd_neighbour[NEXT], buffer_control, CONTROL_PACKET_SIZE, MSG_WAITALL);
	if (ret == -1)
		exit_throw("send()", errno);
}

static int open_data_socket(struct sockaddr_in *recipient_address, unsigned long hops)
{
	enum peer_connection_type type = DATA;
	uint8_t raw_byte = type;
	struct timeval zero = {0};
	int ret;

	// → 1. Apro una connessione di tipo DATA verso il peer che ne à fatto richiesta
	int fd_data_stream = socket(AF_INET, SOCK_STREAM, 0);
	if (fd_data_stream == -1)
		exit_throw("socket()", errno);

	// Prima di fare la connect su socket bloccante imposto un timeout per evitare
	// di bloccare il programma.
	// Alternativamente posso creare il socket non bloccante, fare la connect e attendere che sia
	// pronto in scrittura.
	ret = setsockopt(fd_data_stream, SOL_SOCKET, SO_SNDTIMEO, &connect_flooding_recipient_timeout, sizeof(struct timeval));
	if (ret == -1)
		exit_throw("setsockopt()", errno);

	// Mi connetto
	ret = connect(fd_data_stream, (const struct sockaddr *) recipient_address, sizeof(struct sockaddr_in));
	if (ret == -1 && errno != EWOULDBLOCK && errno != EINPROGRESS && errno != ECONNREFUSED)
		exit_throw("connect()", errno);

	if (ret == -1 && (errno == EWOULDBLOCK || errno == EINPROGRESS || errno == ECONNREFUSED))
	{
		char const *const error = errno != ECONNREFUSED ? "in timeout" : "rifiutata";
		print_warning("connect() verso %s %s. Nodo disconesso? (hop=%lu)", sockaddr_in_to_string(recipient_address), error, hops);
		close(fd_data_stream);
		return -1;
	}

	// Disabilitare il timeout
	ret = setsockopt(fd_data_stream, SOL_SOCKET, SO_SNDTIMEO, &zero, sizeof(struct timeval));
	if (ret == -1)
		exit_throw("setsockopt()", errno);

	// Connessione di tipo DATA
	ret = send(fd_data_stream, &raw_byte, 1, 0);
	if (ret == -1)
		exit_throw("write()", errno);

	return fd_data_stream;
}