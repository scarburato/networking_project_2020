#!/bin/bash


# Variabili
TERMINAL_EMULATOR=gnome-terminal
PEER_START_PORT=5600
DS_PEERS_CARDINALITY=5
use_gdb=0
generate_new_db="S"

printf "Compilo il programma...\n"
make clean
make all -j2
printf "Fatto!\n"

chmod +x peer.sh
chmod +x ds.sh

printf "Dopo 3+3+3 secondi proseguirò in automatico con dei valori predefiniti!\n"
read -t 3 -p "Avviare anche il ds? [S/n] " boot_ds
read -t 3 -p "Avviare con gdb? [s/N] " use_gdb
read -t 3 -p "Generare nuove entry? [S/n]" generate_new_db

if [ "$use_gdb" == "s" ]; then
	printf "!!!!!!!!!!!!!!!!!!!!!!!!! ATTENZIONE !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
	printf "Se GDB non è installato i peer non partiranno!\n"
	printf "Se questo è il caso selezione N ovvero installare GDB!\n\n"
	use_gdb=1
fi
echo $use_gdb
export use_gdb

if [ "$generate_new_db" != "n" ]; then
	for i in $(seq $DS_PEERS_CARDINALITY); do
		rm -r "config/peer-$((PEER_START_PORT + i))"
		env PEER_WORK_DIR="./config/peer-$((PEER_START_PORT + i))" ./build/generator $i
	done
fi

if [ "$boot_ds" != "n" ]; then
	export DS_PEERS_CARDINALITY
	$TERMINAL_EMULATOR -- ./ds.sh
	sleep 0.30
fi

for i in $(seq $DS_PEERS_CARDINALITY); do
	env ID=$i PEER_WORK_DIR="./config/peer-$((PEER_START_PORT + i))" $TERMINAL_EMULATOR -- ./peer.sh $((PEER_START_PORT + i)) &
done

sleep 1.75
printf "\n"
ps -fC peer

