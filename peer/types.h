//
// Created by dario on 10/02/21.
//

#ifndef PROGETTO_CLION_TYPES_H
#define PROGETTO_CLION_TYPES_H

enum entry_type {SWAB, NEW_CASE};

/**
 * Le informazioni su una query
 */
struct query
{
	enum {INVALID = 0, TOTAL, VARIATION} aggr;
	enum entry_type         type;
	time_t                  start;
	time_t                  end;
};

struct query_result
{
	enum {INVALID_RESULT = 0, SUCCESS, MISSING_ENTIRES_ASK_NET_CACHE, MISSING_ENTRIES_FLOOD} status;
	union {
		/**
		 * Se MISSING_ENTRIES_* è usata per dare il riferimento
		 * al chiamante dell'inizio del flooding
		 */
		time_t reference;
		/**
		 * Se SUCESS => Risulato di una query di tipo SOMMA
		 */
		long long result;

	};
};

/**
 * Voce di registro
 */
struct data_register_entry
{
	peer_definitive_id      author;
	time_t                  date;
	enum entry_type         type;
	uint16_t                quantity;
};

struct data_register{
	struct data_register_entry const *entries;
	size_t length;
	size_t real_length;
};



#endif //PROGETTO_CLION_TYPES_H
