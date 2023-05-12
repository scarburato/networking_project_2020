//
// Created by dario on 26/01/21.
//

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <sys/mman.h>
#include "disk.h"
#include "query.h"

#define REGISTER_LENGTH 100

/**
 * Legge il mio id nel registro
 * @param fd_file_register
 * @return 0 se non è ancora stato impostato.
 */
static bool peers_register_get_my_id(struct disk_status*);

static time_t cmpfunc (const void * a, const void * b) {
	return ( *(time_t*)a - *(time_t*)b );
}

struct disk_status disk_init(short postfix)
{
	struct disk_status newobj;

	char tmp[255];
	char const *work_dir_name = getenv("PEER_WORK_DIR");
	int ret;

	if(!work_dir_name)
	{
		sprintf(tmp, "./config/generic/peer-%us", postfix);
		work_dir_name = tmp;
	}

	print_notice("Cartella di lavoro che userò: %s", work_dir_name);

	ret = mkdir(work_dir_name, 0777);
	if(ret == -1 && errno != EEXIST)
		exit_throw("mkdir()", errno);

	newobj.fd_base_dir = open(work_dir_name, O_DIRECTORY, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
	if(newobj.fd_base_dir == -1)
		exit_throw("open()", errno);

	newobj.fd_file_register = openat(newobj.fd_base_dir, "peers_register", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
	if(newobj.fd_file_register == -1)
		exit_throw("openat()", errno);

	ret = peers_register_get_my_id(&newobj);
	print_notice("Ho letto il mio id da disco. È %u", newobj.my_id);
	if(!ret)
		exit_throw("peers_register_get_my_id()", errno);

	// Creo un "buco" in modo che nel registro di stato si legga sempra 1
	peers_register_set_last_update(&newobj, PEER_DEFINITIVE_ID_MAX + 1, 0xdeadbeafdeadbeaf);

	// Ora salvo una lista di registri in una cartella
	newobj.__registers_allocated = REGISTER_LENGTH;
	newobj.registers_length = 0;

	// Per semplicità non potrò mai avere più di REGISTER_LENGTH registri
	newobj.registers = calloc(REGISTER_LENGTH, sizeof(time_t));

	// Listo tutti i file nella dir
	DIR *root = fdopendir(newobj.fd_base_dir);
	for(struct dirent *dp = readdir(root); dp;dp = readdir(root))
	{
		char day[3] = {0};
		char month[3] = {0};
		char year[5] = {0};

		if(!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") || strlen(dp->d_name) > 8)
			continue;
		//puts(dp->d_name);

		if(newobj.registers_length >= newobj.__registers_allocated - 1)
			exit_throw("Troppi file di registro. Più di quanti ne possa gesitre!", ENOMEM);

		// Copio i pezzi di string
		memcpy(year, dp->d_name, 4);
		memcpy(month, dp->d_name + 4, 2);
		memcpy(day, dp->d_name + 4 + 2, 2);

		struct tm d = {
			.tm_mday = atoi(day),
			.tm_mon =  atoi(month) - 1,
			.tm_year =  atoi(year) - 1900,
			.tm_isdst = -1
		};
		newobj.registers[newobj.registers_length] = mktime(&d) - __timezone;
		newobj.registers_length ++;
	}

	qsort(newobj.registers, newobj.registers_length, sizeof(time_t), (__compar_fn_t) cmpfunc);

	return newobj;
}

static bool peers_register_get_my_id(struct disk_status *const ds)
{
	off_t seek_ret;
	ssize_t read_ret;

	struct stat buf;
	read_ret = fstat(ds->fd_file_register, &buf);
	if(read_ret == -1)
		return 0;

	if(!buf.st_size)
	{
		ds->my_id = 0;
		return 1;
	}

	errno = 0;

	seek_ret = lseek(ds->fd_file_register, 0, SEEK_SET);
	if(seek_ret == -1)
		return 0;

	read_ret = read(ds->fd_file_register, &ds->my_id, sizeof(peer_definitive_id));
	if(read_ret < sizeof(peer_definitive_id))
		return 0;

	printf("%u\n", ds->my_id);

	return 1;
}

bool peers_register_set_my_id(struct disk_status *const ds, peer_definitive_id my_id)
{
	off_t seek_ret;

	errno = 0;

	seek_ret = lseek(ds->fd_file_register, 0, SEEK_SET);
	if(seek_ret == -1)
		return false;

	seek_ret = write(ds->fd_file_register, &my_id, sizeof(peer_definitive_id)) != -1;
	if(seek_ret)
		ds->my_id = my_id;

	return seek_ret;
}

void peers_register_set_last_update(struct disk_status *const ds, peer_definitive_id id, time_t last_update)
{
	off_t ret;

	// If data is later written at this point, subsequent reads of the data in the
	// gap (a "hole") return null bytes ('\0') until data is actually written into the gap.
	// Alcuni file system (ext4, btrfs) non scrivono neanche il "buco" su disco, risparmiando
	// spazio.
	ret = lseek(ds->fd_file_register, id*sizeof(struct peers_register_entry) + sizeof(peer_definitive_id), SEEK_SET);
	if(ret == id*sizeof(struct peers_register_entry) + sizeof(peer_definitive_id) - 1)
		exit_throw("Errore su lseek", errno);

	// Scrivo
	ret = write(ds->fd_file_register, &(struct peers_register_entry) {
		.last_update = last_update
	}, sizeof(struct peers_register_entry));
	if(ret == -1)
		exit_throw("Errore su write", errno);
}

time_t peers_register_get_last_update(struct disk_status *const ds, peer_definitive_id id)
{
	off_t ret;
	struct peers_register_entry r;

	// se sono io, sono sempre aggiornato!
	if(ds->my_id == id)
		return time(0) - 1;

	ret = lseek(ds->fd_file_register, id*sizeof(struct peers_register_entry) + sizeof(peer_definitive_id), SEEK_SET);
	if(ret != id*sizeof(struct peers_register_entry) + sizeof(peer_definitive_id))
		return 0;

	ret = read(ds->fd_file_register, &r, sizeof(struct peers_register_entry));
	if(ret == -1)
		exit_throw("Errore su read", errno);

	return r.last_update;
}

time_t peers_register_get_lowest_last_update(struct disk_status *ds, const peer_definitive_id max_id)
{
	off_t  ret;
	struct peers_register_entry r;
	time_t min_t = 0;

	ret = lseek(ds->fd_file_register, sizeof(peer_definitive_id), SEEK_SET);
	if(ret != sizeof(peer_definitive_id))
		return 0;

	for(peer_definitive_id i = 1; i <= max_id; i++)
	{
		ret = read(ds->fd_file_register, &r, sizeof(struct peers_register_entry));
		if(ret != sizeof(struct peers_register_entry))
		{
			print_warning("EOF premautro nel registro stato @ %ld ?", ret);
			return 0;
		}

		min_t = min(min_t, r.last_update);
	}

	return min_t;
}

struct data_register open_data_register(struct disk_status *const ds, const time_t date)
{
	char file_name[10] = {0};
	struct data_register ret = {0};
	int fd_file;
	bool trovato = 0;
	time_t date_at_midnight = date - (date % (60 * 60 * 24));

	// prima di tutto, esiste?
	for(size_t i = 0; !trovato && i < ds->registers_length; i++)
		trovato = ds->registers[i] == date_at_midnight;

	if(!trovato)
		return ret;

	// Apertura registro
	strftime(file_name, 10, "%Y%m%d", gmtime(&date));
	fd_file = openat(ds->fd_base_dir, file_name, O_RDWR , 0);
	if(fd_file == -1)
		return ret;

	// Lunghezza attuale registro
	ret.real_length = lseek(fd_file, 0L, SEEK_END) + 1;
	if(ret.real_length == 0)
		exit_throw("lseek()", errno);
	ret.length = ret.real_length / sizeof(struct data_register_entry);

	// Mappo in memoria in sola lettura!
	ret.entries = mmap(NULL, ret.real_length, PROT_READ, MAP_SHARED, fd_file, 0);
	if((char*)ret.entries == (char*)-1)
		return (struct data_register){0};

	// Ora posso chiudere il file
	close(fd_file);
	return ret;
}

void close_data_register(struct data_register *reg)
{
	if(!reg->entries)
		return;

	int ret = munmap((void*)reg->entries, reg->real_length);
	if(ret == -1)
		exit_throw("munmap()", errno);
}

int data_register_add_entry(struct disk_status *const ds, struct data_register_entry const *entry)
{
	ssize_t ret;
	int fd_file;
	char file_name[10] = {0};
	bool trovato = 0;

	strftime(file_name, 10, "%Y%m%d", gmtime(&entry->date));

	// Apertura del file
	fd_file = openat(ds->fd_base_dir, file_name, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
	if(fd_file == -1)
	{
		ret = errno;
		return 0;
	}

	// Se non è nella lista lo aggiungo
	// Copio i pezzi di string
	char year[5] = {0}, month[3] = {0}, day[3] = {0};
	time_t t;
	memcpy(year, file_name + 0, 4);
	memcpy(month, file_name + 4, 2);
	memcpy(day, file_name + 4 + 2, 2);

	struct tm d = {
		.tm_mday = atoi(day),
		.tm_mon =  atoi(month) - 1,
		.tm_year =  atoi(year) - 1900,
		.tm_isdst = -1
	};
	t = mktime(&d) - __timezone;

	for(size_t i = 0; i < ds->registers_length && !trovato; i++)
		trovato = ds->registers[i] == t;

	if(!trovato)
	{
		ds->registers[ds->registers_length] = mktime(&d) - __timezone;
		ds->registers_length ++;
	}

	// Scrivo
	ret = write(fd_file, entry, sizeof(struct data_register_entry));
	if(ret == -1)
		ret = errno;
	else
		ret = 0;

	close(fd_file);
	return ret;
}

struct previous_last_update {
	peer_definitive_id id;
	time_t time;
};

bool transmit_data(int fd_data_stream, struct disk_status *const ds, struct ds_connection *dc, peer_definitive_id target, time_t lower_bound, time_t upper_bound)
{
	int ret;
	uint64_t packet[4];
	uint64_t length = 0;
	//time_t now = time(0);
	//if(upper_bound > now)
	//	upper_bound = now;

	if(target)
		length = 1;
	else
	{
		for(peer_definitive_id i = 1; i <= dc->max_id; i++)
			// Se il mio ultimo aggiornamento è anteriore al suo ultimo aggiornamento
			// allora dovrò inviare voci di registro aggiornate
			if(lower_bound <= peers_register_get_last_update(ds, i))
				length ++;
	}

	// Quanti peer e con che timestamp andremo ad aggiornare
	length = htong(length);
	ret = send(fd_data_stream, &length, sizeof(uint64_t), 0);
	if(ret == -1)
		exit_throw("send()", errno);

	// Spero gcc sappia ridurre il codice duplicato qua
	if (target)
	{
		time_t last_update = peers_register_get_last_update(ds, target);

		// id target
		packet[0] = htong(target);
		// lower bound del mio aggiornamento
		packet[1] = htong(lower_bound);
		// upper bound del mio aggiornamento
		packet[2] = htong(min(last_update, upper_bound));

		ret = send(fd_data_stream, packet, sizeof(uint64_t) * 3, 0);
		if(ret == -1)
			exit_throw("write()", errno);
	}
	else
		for(peer_definitive_id i = 1; i <= dc->max_id; i++)
		{
			time_t last_update = peers_register_get_last_update(ds, i);

			// non posso inviare voci di auturi non annunciati
			if(lower_bound > last_update)
				continue;

			// id i
			packet[0] = htong(i);
			// lower bound del mio aggiornamento
			packet[1] = htong(lower_bound);
			// upper bound del mio aggiornamento
			packet[2] = htong(min(last_update, upper_bound));

			ret = send(fd_data_stream, packet, sizeof(uint64_t) * 3, 0);
			if(ret == -1)
				exit_throw("write()", errno);
		}

	// → 2. Itero tutti i registri da lowerbound a upper bound
	bool pipe_broken = 0;
	for(time_t day = lower_bound; !pipe_broken && day <= upper_bound; day += 60*60*24)
	{
		struct data_register reg = open_data_register(ds, day);
		if(!reg.entries)
			continue;

		// → 3. Per ogni registro intero tutte le entry. Quelle che rientrano nel bound vengono inviate
		for(size_t i = 0; i < reg.length; i++)
		{
			// Se è specificato l'autore scarto le query fuori
			if(target && reg.entries[i].author != target)
				continue;

			// Bound
			if(reg.entries[i].date < lower_bound || reg.entries[i].date > upper_bound)
				continue;

			// → 4. Spedisco al mio destinatario
			prepare_data_register_entry(packet, reg.entries + i);
			ret = send(fd_data_stream, packet, sizeof(uint64_t) * 4, 0);
			if(ret == -1 && errno == EPIPE)
			{
				pipe_broken = 1;
				print_warning("Flood terminata precocemente @ %lu", reg.entries[i].date);
				continue;
			}
			if(ret == -1)
				exit_throw("write()", errno);
		}

		close_data_register(&reg);
	}

	return !pipe_broken;
}

void recive_data(int fd_data_stream, struct disk_status *const ds)
{
	/**
	 * @FIXME Se il socket crasha il disco viene lasciato in uno stato non
	 * consistente!!!!!!
	 */
	int ret;
	uint64_t length = 0;
	struct previous_last_update *l;

	// Primo elemento: quanti andrò ad aggiornare
	ret = recv(fd_data_stream, &length, sizeof(uint64_t), MSG_WAITALL);
	if (ret == -1)
		exit_throw("recv()", errno);
	if (ret < sizeof(uint64_t))
	{
		print_warning("Violazione del protocollo, aggiornamento fallito! ret=%d", ret);
		return;
	}

	length = ntohg(length);
	l = calloc(length, sizeof(struct previous_last_update));
	if(!l)
		exit_throw("calloc()", ENOMEM);

	// I nuovi margini temporali di aggiornamento dei peer
	for (uint64_t i = 0; i < length; i++)
	{
		uint64_t buffer[3];
		peer_definitive_id id;
		time_t lower_bound, upper_bound, my_low;

		ret = recv(fd_data_stream, buffer, sizeof(uint64_t) * 3, MSG_WAITALL);
		if (ret == -1)
			exit_throw("recv()", errno);
		if (ret < sizeof(uint64_t))
			exit_throw("ret < sizeof 64bit * 3", EPROTO);

		id = ntohg(buffer[0]);
		lower_bound = ntohg(buffer[1]);
		upper_bound = htong(buffer[2]);

		my_low = peers_register_get_last_update(ds, id);
		if (my_low < lower_bound)
		{
			print_warning("Non posso accettare questo flusso di data! Il mio low = %lu < del suo low = %lu", my_low, lower_bound);
			free(l);
			return;
		}

		l[i] = (struct previous_last_update) {
			.id = id,
			.time = my_low
		};

		peers_register_set_last_update(ds, id, upper_bound);
	}

	// Seconda parte del proto: arrivano le voci di registro aggiornate!
	do
	{
		uint64_t buffer[4];
		struct data_register_entry new_come;
		size_t peer_index;

		ret = recv(fd_data_stream, buffer, sizeof(uint64_t)*4, MSG_WAITALL);
		if (ret == 0)
			continue;
		if (ret == -1)
			exit_throw("recv()", errno);
		if (ret < sizeof(uint64_t)*4)
			exit_throw("ret < sizeof 64bit * 4", EPROTO);

		new_come = parse_data_register_entry(buffer);

		// Ricerco il vecchio timestamp d'aggiornamento del peer
		// Se la entry è posteriore a quel timestamp la scarto per evitare duplicati
		for(peer_index = 0;peer_index < length && l[peer_index].id != new_come.author; peer_index++)
			;

		if(peer_index >= length)
		{
			print_error("Voce di registro %hu non era stata annunciata", new_come.author);
			exit_throw("Impossibile trovarlo in l", EPROTO);
		}

		// Questo scarta i miei duplicati
		if(new_come.date <= l[peer_index].time)
		{
			//if (new_come.author != ds->my_id)
			//	print_warning("Voce di registro è del %lu che è posteriore a %lu. Scartata", new_come.date, l[peer_index].time);
		}
		else
			data_register_add_entry(ds, &new_come);
	}
	while(ret);

	free(l);
}