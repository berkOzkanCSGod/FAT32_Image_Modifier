all:  fatmod
	clear

fatmod: fatmod.c
	gcc -Wall -g -o fatmod fatmod.c

clean: 	
	rm -fr *~ fatmod