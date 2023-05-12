//
// Created by dario on 26/01/21.
//

#ifndef PROGETTO_CLION_DISK_H
#define PROGETTO_CLION_DISK_H

#include <stdio.h>
#include <time.h>
#include "../common.h"
#include "../error.h"
#include "types.h"

/**
 * Questa è una entry dell'ultimo aggiornamento del peer.
 * Serve per sapere se i dati non sono aggiornati
 */
struct peers_register_entry
{
	time_t        last_update;
};

struct disk_status
{
	int fd_base_dir;
	int fd_file_register;

	peer_definitive_id my_id;

	/** Nomi di tutti i registri presenti su disco */
	time_t *registers;
	size_t registers_length;
	size_t __registers_allocated;
};

struct ds_connection;

/**
 * 1. Prepara la cartella di lavoro del peer.
 * Prova ad usare la path definitia nella
 * variabile d'ambiente PEER_WORK_DIR
 * 2. Apre il file che contiene le informazioni della rete p2p
 *
 * @param il postiffiso della cartella
 */
struct disk_status disk_init(short postfix);

/**
 * Distruttore
 * @param ds
 */
__always_inline void disk_exit(struct disk_status *const ds)
{
	close(ds->fd_file_register);
	close(ds->fd_base_dir);

	free(ds->registers);
}

/**
 * Salva il nuovo id;
 * @param fd_file_register
 * @param my_id
 */
bool peers_register_set_my_id(struct disk_status*, peer_definitive_id);

/**
 * Imposta la data dell'ultimo aggiornamento di un peer della rete
 * @param fd_file_register Il fd del file di registro
 * @param id ID del peer target
 * @param last_update
 */
void peers_register_set_last_update(struct disk_status*, peer_definitive_id id, time_t last_update);

/**
 * Mappa un registro in memoria
 * @param date
 * @return Se il registro non esiste torna {0}
 */
struct data_register open_data_register(struct disk_status *ds, time_t date);

/**
 * "Smonta" un registro dalla memoria
 * @param reg
 */
void close_data_register(struct data_register *reg);

/**
 * Aggiunge una entry al registro
 * @param fd_base_dir La cartella che contiene i registri di questo peer
 * @param entry La voce da inserire nel registro
 * @return 0 se successo, codice d'errore altrimenti
 */
int data_register_add_entry(struct disk_status*, struct data_register_entry const *entry);

/**
 * In che data un peer è stato aggiornato.
 * Attenzione: non è la data dell'ultima entry del peer salvata su disco (sarà minore o eguale)
 * ne la data di scaricamento dal vicino (che sarà maggiore o eguale. Eguale solo se il peer e
 * vicino concidono ovviamente)
 * @param ds Informazioni sul disco in uso
 * @param id id di interesse
 * @return
 */
time_t peers_register_get_last_update(struct disk_status *ds, peer_definitive_id id);

/**
 * @see peers_register_get_last_update ma torna il minimo possibile
 */
time_t peers_register_get_lowest_last_update(struct disk_status *ds, peer_definitive_id max_id);

/**
 *
 * @param fd_data_stream
 * @param ds
 * @param dc
 * @param target il target. se è 0 coinvole tutti i peer del registro peer!
 * @param lower_bound
 * @param upper_bound
 * @return 1 se successo 0 se si è rotto il tubo
 */
bool transmit_data(int fd_data_stream, struct disk_status *ds, struct ds_connection *dc, peer_definitive_id target, time_t lower_bound, time_t upper_bound);

/**
 * Funzione bloccate. Prende un socket associato a un flusso DATA e
 * aggiorna i registri.
 *
 */
void recive_data(int fd_data_stream, struct disk_status *ds);

#endif //PROGETTO_CLION_DISK_H
