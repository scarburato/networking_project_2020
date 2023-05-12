#include "prompt.h"
#include "../error.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

static const char *const args_type_string[] = {
	"string",
	"int32",
	"uint32"
};

void dprintprompt(int fd, struct command const *const commands, size_t commands_length, bool stamp_prompt)
{
	dprintf(fd, "Comandi processabili:\n");

	for(size_t i = 0; stamp_prompt && i < commands_length; i++)
	{
		dprintf(fd, "→ %s\t", commands[i].name);

		for(size_t j =0 ; j < commands[i].arguments_length; j++)
			dprintf(fd, "\t%s:%s ",args_type_string[commands[i].arguments[j].type], commands[i].arguments[j].name);

		dprintf(fd, "\t%s\n", commands[i].description);
	}
	dprintf(fd, "> ");
}

size_t parsecommand(char const *const parse, struct argument_given *arguments_given, struct command const *const commands, size_t commands_length)
{
	/** La posizione degli spazi nella stringa parse */
	size_t seperator_positions[MAX_ARGUMENTS + 1] = {0};
	size_t seperator_positions_length = 0;

	/** Lunghezza stringa */
	size_t parse_length = 0;

	/** Puntatore al comando richiesto, se esiste */
	struct command const *requested_command = 0;
	size_t req_index = -1;

	// Azzero il vettore
	memset(arguments_given, 0x00, sizeof(struct argument_given)*MAX_ARGUMENTS);

	if(parse[0] == 0x00)
		return -1;

	// Alla ricerca dei separatori
	for(; parse_length < SIZE_MAX && parse[parse_length]; parse_length++)
		if(isspace(parse[parse_length]))
		{
			// Troppi argomenti!! Esco
			if(seperator_positions_length == MAX_ARGUMENTS)
			{
				fputs("Troppi argomenti!\n", stderr);
				return -1;
			}

			seperator_positions[seperator_positions_length] = parse_length;
			seperator_positions_length++;
		}

	// Metto un separatore anche a '\0' per comodità, se la stringa non termina già con un sep.
	if(parse_length && !isspace(parse[parse_length - 1]))
	{
		seperator_positions[seperator_positions_length] = parse_length;
		seperator_positions_length++;
	}

	// Ogni sottostringa DEVE avere lunghezza <= MAX_STR_LEN - 1
	for(size_t i = 0, prev_pos = 0; i < seperator_positions_length; i++)
	{
		if(seperator_positions[i] - prev_pos > MAX_STR_LEN - 1)
		{
			fputs("L'argomento eccede MAX_STR_LEN-1\n", stderr);
			return -1;
		}

		prev_pos = seperator_positions[i];
	}

	// Ricerco il comando
	for(size_t i = 0; i < commands_length && !requested_command; i++)
		if(!memcmp(parse, commands[i].name, seperator_positions[0]))
		{
			requested_command = commands + i;
			req_index = i;
		}

	if(!requested_command)
	{
		print_error("Comando `%s' non riconosicuto\n", parse);
		return -1;
	}

	if(seperator_positions_length <= requested_command->arguments_length)
	{
		print_error("Troppi pochi argomenti: erano %zu anziché %zu\n", seperator_positions_length - 1, requested_command->arguments_length);
		return -1;
	}

	for(size_t i = 0; i < requested_command->arguments_length; i++)
	{
		char const *const arg = parse + seperator_positions[i] + 1;
		size_t const arg_len = seperator_positions[i+1] - seperator_positions[i] - 1;

		/**
		 * Since  strtoul() can legitimately return 0 or ULONG_MAX (ULLONG_MAX for
		 * strtoull()) on both success and failure, the calling program should set
		 * errno  upper_bound 0 before the call, and then determine if an error occurred by
		 * checking whether errno has a nonzero value after the call.
		 */
		errno = 0;

		switch(requested_command->arguments[i].type)
		{
		case STRING:
			memcpy(arguments_given[i].string, arg, arg_len);
			arguments_given[i].valid_parse = true;
			break;
		case INT:
			arguments_given[i].integer = strtoll(arg, 0, 0);
			arguments_given[i].valid_parse = !errno;
			break;
		case UINT:
			arguments_given[i].unsigned_integer = strtoull(arg, 0, 0);
			arguments_given[i].valid_parse = !errno;
			break;
		}
	}

	return req_index;
}