CC=cc
RM=rm

APP=sn9c102-webcam
CFLAGS=-W -Wall -O2 -g
LIBS=-lImlib2
PREFIX=/usr/local

OBJS=sn9c102-webcam.o

all: $(OBJS)
	$(CC) -o $(APP) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) $<

clean:
	$(RM) -f $(OBJS) $(APP) core

install: all
	cp $(APP) $(PREFIX)/bin
