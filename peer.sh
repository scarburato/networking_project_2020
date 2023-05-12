#!/bin/bash
## Questo script serve solo a fare la start automatica verso 127.0.0.1:7575
## e passare poi stdin alla telescrivente

printf "\033]0;[$ID] TTY DEL PEER $PEER_WORK_DIR\007"

if [ "$use_gdb" == "1" ]; then
	#printf "start 127.0.0.1 7575\n" > "/tmp/dirty$1"
	gdb ./build/peer -q -ex "handle SIGINT stop" -ex "run $1 autoboot"
else
	./build/peer $1 autoboot
fi

