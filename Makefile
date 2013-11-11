EXECS = piarcade
CC    = gcc -I/usr/local/include -L/usr/local/lib -lm -lwiringPi

all: $(EXECS)

piarcade: piarcade.c
	$(CC) $< -o $@
	strip $@

clean:
	rm -f $(EXECS)
