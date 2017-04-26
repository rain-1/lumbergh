all: supervisor

supervisor: supervisor.c
	gcc -g -o supervisor supervisor.c

clean:
	rm -f supervisor
