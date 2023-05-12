#!/bin/bash

printf '\e]10;black\a\e]11;#abcdef\a'
printf "\033]0;============= TTY DISCOVERY SERVER =============\007"


if [ "$use_gdb" == "1" ]; then
	gdb ./build/ds -q -ex "handle SIGINT stop" -ex "b exit" -ex "run 7575"
else
	./build/ds 7575
fi

