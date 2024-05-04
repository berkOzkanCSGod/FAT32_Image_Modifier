all:  fatmod

fatmod: fatmod.c
	gcc -Wall -g -o fatmod fatmod.c

clean: 	
	rm -fr *~ fatmod