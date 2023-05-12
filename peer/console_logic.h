//
// Created by dario on 07/01/21.
//

#ifndef PROGETTO_CLION_PEER_CONSOLE_LOGIC_H
#define PROGETTO_CLION_PEER_CONSOLE_LOGIC_H

#include "./net.h"
#include "../shared/prompt.h"
#include "../common.h"
#include "disk.h"
#include "query.h"

struct data_register_entry;

struct peer_console
{
	bool hasPrintConsole;

	union {
		struct sockaddr_in ds_address;
		struct data_register_entry entry;
		struct query q;
	};
};

// Tieni sincronizzato con gli indici di commands
enum peer_console_command
{
	CONSOLE_COMMAND_START = 0u,
	CONSOLE_COMMAND_ADD,
	CONSOLE_COMMAND_GET,
	CONSOLE_COMMAND_SHOW,
	CONSOLE_COMMAND_SHOW_STATE,
	CONSOLE_COMMAND_TEST,
	CONSOLE_HELP,
	CONSOLE_QUIT,

	CONSOLE_AGAIN = 0xff
};

struct ds_connection;
void peer_console_show(struct peer_console *state, struct ds_connection *ds_state);

enum peer_console_command peer_console_dispatcher(struct peer_console *state);

#endif //PROGETTO_CLION_CONSOLE_LOGIC_H
