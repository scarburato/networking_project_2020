//
// Created by dario on 01/01/21.
//

#ifndef PROGETTO_CLION_PEERS_INFO_H
#define PROGETTO_CLION_PEERS_INFO_H
#include <arpa/inet.h>
#include "../common.h"

/**
 * Informazioni su di un peer
 */
struct peer
{
	peer_definitive_id      id;
	union {
		/** Se sono il DS:
	 	* 	- vale {0} se ho ricevuto un TYPE_PEER_ACK_NEIGHBORS,
	 	* 	- vale t se sto aspettando un TYPE_PEER_ACK_NEIGHBORS dell'aggiornamento dei vicini che ho fatto a t
	 	*/
		struct timeval  last_event;
		/** Se sono il peer:
		 * 	- DA quando mi servono nuove entries nel flooding (si veda query.c)
		 * 	- Il margine temporale inferiore (si deva disk.c)
		 */
		time_t          last_update;
	};


	union {
		/** Se sono il DS: Ultimo segno di vita tra DS e PEER */
		struct timeval  last_heartbeat;
		/** Se sono il peer: fino a quando devo inviare entries nel canale DATA */
		//time_t          upper_bound;
	};
};

/**
 * Elemento di una lista di peer ordinata per address_id
 */
struct peer_node
{
	/** Usata
	 * 1. per conservare la struct sockaddr_in già pronta alla serializzazione. Occupa meno spazio
	 * 2. per il criterio d'ordinamento per porta su localhost <= peer_sessio_id = IPv4 << 16 + porta
	 */
	peer_session_id         address_id;
	struct peer             value;
	struct peer_node        *greater_eq;
	struct peer_node        *lesser;
};

/**
 * Aggiunge un peer ad una listas di peer
 * @param root		Riferimento alla radice della lista
 * @param id		ID del nodo, dovrebbe essere univoco
 * @param session_id
 * @return Il puntatore al nuovo nodo.
 */
struct peer_node * peer_list_add(struct peer_node **root, peer_definitive_id id, peer_session_id session_id);

/**
 * Cancella un nodo da una lista di nodi.
 * Il target viene free(target) al termine della funziona.
 * @param root		La radice della lista
 * @param target	Il nodo della lista da eliminare. Ovviamente deve far parte della lista
 * 			**Al ritorno il parametro target non è più valido**
 */
void peer_list_remove(struct peer_node **root, struct peer_node *target);

struct peer_node* peer_list_find(struct peer_node *root, peer_session_id session_id);

void peer_list_delete(struct peer_node *root);

struct peer_node* peer_list_get_tail(struct peer_node *root);

size_t peer_list_size(struct peer_node const*);

/**
 * Calcola i vicini di un nodo
 * @param root La lista
 * @param t il target
 * @param neighors Vettore di due putatori, conterrà i due vicini.
 * 	pos 0 -> precedente
 * 	pos 1 -> anteriore
 */
void peer_list_get_neighbors(struct peer_node *root, struct peer_node *t, struct peer_node **neighors);
#endif //PROGETTO_CLION_PEERS_INFO_H
