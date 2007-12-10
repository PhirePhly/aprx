#
#  APRSG-NG
#

# easy way to have 1 minute ERLANG data included:  make ERLANG1=1
ifdef ERLANG1
ERLANG="-DUSE_ONE_MINUTE_INTERVAL=1"
endif

CC=	gcc 
CFLAGS=	-g -O3  -Wall

SBINDIR=/usr/sbin/
MANDIR=/usr/share/man/man8/
CFGDIR=/etc/

LIBS=	# Nothing special needed!
HDRS=	aprsg.h
SRC=	aprsg.c ttyreader.c ax25.c aprsis.c beacon.c config.c netax25.c erlang.c
OBJS=	aprsg.o ttyreader.o ax25.o aprsis.o beacon.o config.o netax25.o erlang.o

all:  aprsg

install:
	install -c -m 755 aprsg $(SBINDIR)
	install -c -m 644 aprsg-ng.conf $(CFGDIR)
	install -c -m 644 aprsg.8 $(MANDIR)

clean:
	rm -f *~ *.o aprsg


aprsg.o: aprsg.c aprsg.h
	$(CC) $(CFLAGS) -c aprsg.c

aprsis.o: aprsis.c aprsg.h
	$(CC) $(CFLAGS) -c aprsis.c

config.o: config.c aprsg.h
	$(CC) $(CFLAGS) -c config.c

erlang.o: erlang.c aprsg.h
	$(CC) $(CFLAGS) -c erlang.c $(ERLANG)

ttyreader.o: ttyreader.c aprsg.h
	$(CC) $(CFLAGS) -c ttyreader.c

ax25.o: ax25.c aprsg.h
	$(CC) $(CFLAGS) -c ax25.c

netax25.o: netax25.c aprsg.h
	$(CC) $(CFLAGS) -c netax25.c


beacon.o: beacon.c aprsg.h
	$(CC) $(CFLAGS) -c beacon.c

aprsg: $(OBJS)
	$(CC) $(CFLAGS) -o aprsg $(OBJS) $(LIBS)
