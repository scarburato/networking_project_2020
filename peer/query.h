//
// Created by dario on 29/01/21.
//

#ifndef PROGETTO_CLION_QUERY_H
#define PROGETTO_CLION_QUERY_H

#include <sys/time.h>
#include "net.h"
#include "types.h"
#include "../shared/peers_info.h"

#define QUERY_COMPUTE_SKIP_CACHE      0b0001
#define QUERY_COMPUTE_SKIP_NEIGHBOURS 0b0010
#define QUERY_COMPUTE_SKIP_FLOODING   0b0100

struct ds_connection;

/**
 * Cerca nella cache se la query è già presente
 * @param q
 * @return il risultato se trovato altrimenti {.status = INVALID_RESULT}
 */
struct query_result query_find_in_cache(const struct query q);

/**
 * Risolve una query
 * @param q La query da risolve
 * @param dks Il disco
 * @param dc Il DS
 * @param flags Quali parti di algoritmo saltare
 * @return Il risulato della query. {0} se errore
 */
struct query_result query_compute(struct query q, struct disk_status *const dks, struct ds_connection *const dc, int neighbour_in, int neighbour_out, struct peer_node **missing, int flags);

/**
 * Stampa sulla telescrivente il risulato della query
 * @param fd_printer
 * @param query_result
 */
void dprintqueryresult(int fd_printer, struct query_result const *);
#endif //PROGETTO_CLION_QUERY_H
