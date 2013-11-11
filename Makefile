CC    = gcc -I/usr/local/include -L/usr/local/lib -lm -lwiringPi

all: piarcade

piarcade: uinput.o piarcade.o
	$(CC) uinput.o piarcade.o -o piarcade

piarcade.o: piarcade.c
	$(CC) -c piarcade.c

uinput.o: uinput.c
	$(CC) -c uinput.c

clean:
	rm -rf *o piarcade
