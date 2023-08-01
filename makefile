all:
	gcc -o BibakBOXServer server.c -pthread -Wall
	gcc -o BibakBOXClient client.c -Wall
clean:
	rm -f *.o BibakBOXServer BibakBOXClient