CC	= gcc
CFLAGS	= -O2 -g -Wall
LIBS	=

CLIENT = nat-punch-client
SERVER = nat-punch-server

all: $(CLIENT) $(SERVER)

.c.o:
	$(CC) $(CFLAGS) -c $<

$(CLIENT): 
	$(CC) -o $(CLIENT) $(CLIENT).c compat/base64.c $(LIBS) -lresolv

$(SERVER): 
	$(CC) -o $(SERVER) $(SERVER).c $(LIBS)

strip: $(CLIENT) $(SERVER)
	strip $(CLIENT)
	strip $(SERVER)

clean:
	rm -f *.o $(CLIENT)
	rm -f *.o $(SERVER)
