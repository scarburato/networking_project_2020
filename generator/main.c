#include <stdio.h>
#include <errno.h>
#include "../common.h"
#include "../error.h"
#include "../peer/disk.h"

// quante entry andrò a creare
#define ENTRIES_CARDINALITY 50
// Su che spazio temporale creare le nuove entry
#define HOURS_MIN 60*60*24*10

/**
 * Questo programmino crea entry casuali per un DS di ID passato
 * come primo argomento
 */
int main(int argc, char** argv)
{
	if(argc < 2)
		exit_throw("Passare un argomento!", ENODATA);

	struct disk_status ds = disk_init(0);
	time_t now = time(0);
	int ret = 0;

	if(ds.my_id == 0)
		peers_register_set_my_id(&ds, atoi(argv[1]));

	srand(time(0));

	for(size_t i = 0; i < ENTRIES_CARDINALITY; i++)
	{
		time_t offset = rand() % HOURS_MIN;

		ret = data_register_add_entry(&ds, &(struct data_register_entry) {
			.author = ds.my_id,
			.date = now - offset,
			.type = rand() % 2 ? SWAB : NEW_CASE,
			.quantity = 5 + (rand() % 10) * 5
		});

		if(ret != 0)
			exit_throw("data_register_add_entry()", ret);
	}

	return 0;
}