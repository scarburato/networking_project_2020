#ifndef PROMPT_H_INCLUDE
#define PROMPT_H_INCLUDE

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define MAX_ARGUMENTS 5
#define MAX_STR_LEN 64
#define MAX_BUF_LEN (MAX_STR_LEN+1)*MAX_ARGUMENTS

/**
 * Tipo di argomento possibile
 */
enum arg_type
{
	STRING,
	INT,
	UINT
};

/**
 * Info su un argomento, tipo e nome
 */
struct argument
{
	enum arg_type type;
	char name[MAX_STR_LEN];
};

/**
 * Contiene informazioni da un prompt
 * @param name il nome del comando es. "start"
 * @param description la descrizione del comando es. "Avvia una connessione col Discovery Server"
 * @param args Se presente, il formato dei params
 */
struct command
{
	char name[MAX_STR_LEN];
	char *description;
	size_t arguments_length;
	struct argument arguments[MAX_ARGUMENTS];
};

struct argument_given
{
	bool valid_parse;
	union 
	{
		char string[MAX_STR_LEN];
		long long integer;
		unsigned long long unsigned_integer;
	};
};

/**
 * Stampa un prompt su file
 * @param fd descrittore di file su cui effettuare output
 * @param commands vettore di comandi che devo stampare nel prompt
 * @param commands_length lunghezza vettore comandi
 */
void dprintprompt(int fd, struct command const *const commands, size_t commands_length, bool stamp_prompt);

/**
 * Cerca di effettura il parsing di un argomento.
 * @param parse La stringa da fare il parse. La sua lunghezza NON DEVE SUPERARE SIZE_MAX
 * @param arugment_gives vettore che DEVE ESSERE lungo MAX_ARGUMENTS. Il cotenuto verrà azzerato.
 * 
 * @return se comando non esiste allora -1 altrimenti l'indice di commands del comando invocato.
 * 	
 */
size_t parsecommand(char const *parse, struct argument_given *arguments_given, struct command const *commands, size_t commands_length);

#endif