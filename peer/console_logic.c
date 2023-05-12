//
// Created by dario on 07/01/21.
//

#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "console_logic.h"
#include "../error.h"
#include "../shared/utility.h"
#include "net.h"

static const struct command commands[] = {
	{
		.name = "start",
		.description = "Connette il peer a un discovery server",
		.arguments = {{STRING, "indirizzo IPv4"}, {UINT, "porta"}},
		.arguments_length = 2
	},
	{
		.name = "add",
		.description = "Aggiunge al registro corrente un nuovo evento",
		.arguments = {{STRING, "tipo"}, {UINT, "quantità"}},
		.arguments_length = 2
	},
	{
		.name = "get",
		.description = "Effettua il computo di un dato aggregato",
		.arguments = {{STRING, "aggr"}, {STRING, "type"}, {STRING, "period"}},
		.arguments_length = 3
	},
	{
		.name = "show",
		.description = "Stampa il contenuto attuale di tutti i registri dati",
		.arguments_length = 0
	},
	{
		.name = "show-status",
		.description = "Stampa il contenuto attuale di tutti i registri stato",
		.arguments_length = 0
	},
	{
		.name = "test",
		.description = "Prova la consistenza dell'anello: invio un messaggio a destra e vedo se torna!",
		.arguments_length = 0
	},
	{
		.name = "help",
		.description = "Stampa aiuto",
		.arguments_length = 0
	},
	{
		.name = "stop",
		.description = "Disconetti dal ds e terminata il programma",
		.arguments_length = 0
	}
};
static const size_t commands_length = 8;

/**
 * NON SICURA!!! NON CONTROLLO LA LUNGHEZZA DELLA STRINGA!!!
 * @param str
 * @return
 */
static struct tm parse_date(char *str)
{
	return (struct tm) {
		.tm_mday = atoi(str),
		.tm_mon = atoi(str + 3) - 1,
		.tm_year = atoi(str + 3 + 3) - 1900,
		.tm_isdst = -1
	};
}

static bool print_prompt = 1;

void peer_console_show(struct peer_console *state, struct ds_connection *ds_state)
{
	if(!state->hasPrintConsole)
	{
		state->hasPrintConsole = true;
		if(ds_state->connected)
			printf("ATTUALMENTE COLLEGATO A DS %s\n", sockaddr_in_to_string(&ds_state->ds_address));
		else
			printf("NON COLLEGATO A DS\n");

		dprintprompt(STDOUT_FILENO, commands, commands_length, print_prompt);
		print_prompt = 0;
	}
}

enum peer_console_command peer_console_dispatcher(struct peer_console *state)
{
	char buffer[MAX_BUF_LEN];
	struct argument_given p[MAX_ARGUMENTS];
	size_t command_selected;
	struct tm date_parsed;
	size_t off;
	int ret;

	//if(!stdin_ready())
	//	return CONSOLE_AGAIN;

	fgets(buffer, MAX_BUF_LEN, stdin);
	state->hasPrintConsole = false;
	if(ferror(stdin))
	{
		print_errno("Lettura da stdin", ferror(STDIN_FILENO));
		return CONSOLE_AGAIN;
	}

	// Ricevuto ^D
	if(feof(stdin))
		return CONSOLE_QUIT;

	command_selected = parsecommand(buffer, p, commands, commands_length);
	if(command_selected == -1)
		return CONSOLE_AGAIN;

	time_t now = time(0);
	switch(command_selected)
	{
	case 0: // start
		if(!p[0].valid_parse || !p[1].valid_parse)
		{
			print_error("Argomento non valido! (%u, %u)",p[0].valid_parse, p[1].valid_parse);
			return CONSOLE_AGAIN;
		}

		memset(&state->ds_address, 0x00, sizeof(struct sockaddr_in));
		ret = inet_pton(AF_INET, p[0].string, &state->ds_address.sin_addr);
		if(ret != 1)
		{
			print_error("`%s' non è un indirizzo IPv4 valido...", p[0].string);
			return CONSOLE_AGAIN;
		}
		state->ds_address.sin_port = htons(p[1].unsigned_integer);
		break;
	case 1: // add
		if(!p[0].valid_parse || !p[1].valid_parse)
		{
			print_error("Argomento non valido! (%u, %u)",p[0].valid_parse, p[1].valid_parse);
			return CONSOLE_AGAIN;
		}

		if( !strcmp("T", p[0].string) || !strcmp("tamponi", p[0].string) || !strcmp("tampone", p[0].string) )
			state->entry.type = SWAB;
		else if ( !strcmp("N", p[0].string) || !strcmp("casi", p[0].string) || !strcmp("caso", p[0].string))
			state->entry.type = NEW_CASE;
		else
		{
			print_error("`%s' non è un tipo valido! Usare `N', `T', `tamponi' ovvero `casi`", p[0].string);
			return CONSOLE_AGAIN;
		}

		state->entry.quantity = p[1].unsigned_integer;
		time(&state->entry.date);

		break;
	case 2: // get
		if(!p[0].valid_parse || !p[1].valid_parse || !p[2].valid_parse)
		{
			print_error("Argomento non valido! (%u, %u, %u)",p[0].valid_parse, p[1].valid_parse, p[2].valid_parse);
			return CONSOLE_AGAIN;
		}

		if(!strcmp("totale", p[0].string))
			state->q.aggr = TOTAL;
		else if (!strcmp("variazione", p[0].string))
			state->q.aggr = VARIATION;
		else
		{
			print_error("`%s' non è una aggregazione valida!", p[0].string);
			return CONSOLE_AGAIN;
		}

		if( !strcmp("T", p[1].string) || !strcmp("tamponi", p[1].string) || !strcmp("tampone", p[1].string) )
			state->q.type = SWAB;
		else if ( !strcmp("N", p[1].string) || !strcmp("casi", p[1].string) || !strcmp("caso", p[1].string) )
			state->q.type = NEW_CASE;
		else
		{
			print_error("`%s' non è un tipo valido! Usare `N', `T', `tamponi' ovvero `casi`", p[1].string);
			return CONSOLE_AGAIN;
		}

		char *const last_arg = p[2].string[0] != '\0' ? p[2].string : "*-*";

		// Parse del periodo
		// @fixme nessun controllo input
		// 1° argomento
		if(last_arg[0] == '*')
		{
			off = 2;
			// Fri Jan 01 2010 00:00:00 GMT+0000
			state->q.start = 1262304000;
		}
		else
		{
			off = 11;
			date_parsed = parse_date(last_arg);
			state->q.start = mktime(&date_parsed) - __timezone;
		}


		// 2° argomento
		if(last_arg[off] == '*')
			// Prendo le 23:59:59 del giorno precedente di oggi
			state->q.end = now - (now % (60 * 60 * 24)) - 1;
		else
		{
			date_parsed = parse_date(last_arg + off);

			// Il margine superiore comprende fino alla fine della giornata.
			date_parsed.tm_hour = 23;
			date_parsed.tm_min = 59;
			date_parsed.tm_sec = 59;
			state->q.end = mktime(&date_parsed) - __timezone;
		}

		if(state->q.start >= state->q.end)
		{
			print_error("Periodo [%lu, %lu] invalido!", state->q.start, state->q.end);
			return CONSOLE_AGAIN;
		}

		if(state->q.end >= now - (now % (60 * 60 * 24)))
			print_warning("Periodo [%lu, %lu] coinvolge REGISTER APERTI ovvero REGISTER FUTURI! La cache verrà disabilitata per query incostistenti!", state->q.start, state->q.end);

		if(state->q.end > now)
			state->q.end = now + (60 * 60 * 24);

		break;
	case 6: // help
		print_prompt = 1;
		return CONSOLE_AGAIN;
	default:
		break;
	}

	return (enum peer_console_command)command_selected;
}
