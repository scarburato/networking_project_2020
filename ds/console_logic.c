//
// Created by dario on 04/01/21.
//

#include <unistd.h>
#include <errno.h>
#include "console_logic.h"
#include "../error.h"
#include "../shared/prompt.h"

static const struct command commands[] = {
	{
		.name = "status",
		.description = "Mostra un elenco di peer connessi",
		.arguments_length = 0
	}, {
		.name = "showneighbor",
		.description = "Mostra i vicini di un peer",
		.arguments = {{ UINT, "peer"}},
		.arguments_length = 1
	}, {
		.name = "quit",
		.description = "Termina eseguzione",
		.arguments_length = 0
	}
};
static const size_t commands_length = 3;

enum ds_console_command ds_console_dispatcher(struct ds_console *state)
{
	char buffer[MAX_BUF_LEN];
	struct argument_given p[MAX_ARGUMENTS];
	size_t command_selected;

	if(!state->hasPrintConsole)
	{
		state->hasPrintConsole = true;
		dprintprompt(STDOUT_FILENO, commands, commands_length, 1);
	}

	if(!stdin_ready())
		return CONSOLE_AGAIN;

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

	if(command_selected == 1)
		state->show_neighbor_of_id = p[0].unsigned_integer;

	return (enum ds_console_command)command_selected;
}
