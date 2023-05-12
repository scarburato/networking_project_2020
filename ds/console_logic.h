//
// Created by dario on 04/01/21.
//

#ifndef PROGETTO_CLION_CONSOLE_LOGIC_H
#define PROGETTO_CLION_CONSOLE_LOGIC_H
#include <stdbool.h>
#include "../shared/peers_info.h"

/**
 * Contiene lo stato di una console del DS tra una iterazione e l'altra.
 * Contiene anche i valori dei parametri dell'ultima chiamata, se usati.
 */
struct ds_console
{
	bool hasPrintConsole;
	union
	{
		peer_definitive_id show_neighbor_of_id;
	};
};

// Tieni sincronizzato con gli indici di commands
enum ds_console_command
{
	CONSOLE_COMMAND_SHOW_PEERS = 0u,
	CONSOLE_COMMAND_SHOW_NEIGHBOR = 1,
	CONSOLE_QUIT = 2,

	CONSOLE_AGAIN = 0xff
};

/**
 * Da chiamare per gestire la console da stdin.
 * @return Il comando dato dall'utente. CONSOLE_AGAIN se non c'era nessun comando
 * 	da leggere ovvero è stato passato un comando non valido
 */
enum ds_console_command ds_console_dispatcher(struct ds_console *state);
#endif //PROGETTO_CLION_CONSOLE_LOGIC_H
