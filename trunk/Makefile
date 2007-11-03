#
#  APRSG-NG
#

CC=	gcc
CFLAGS=	-g

SBINDIR=/usr/sbin
MANDIR=/usr/share/man/man8
CFGDIR=/etc

LIBS=	# Nothing special needed!
HDRS=	aprsg.h
SRC=	aprsg.c ttyreader.c ax25.c aprsis.c beacon.c config.c
OBJS=	aprsg.o ttyreader.o ax25.o aprsis.o beacon.o config.o

main:  aprsg

install: main
	install -c aprsg


clean:
	rm -f *~ *.o aprsg


aprsg.o: aprsg.c aprsg.h
	$(CC) $(CFLAGS) -c aprsg.c

aprsis.o: aprsis.c aprsg.h
	$(CC) $(CFLAGS) -c aprsis.c

config.o: config.c aprsg.h
	$(CC) $(CFLAGS) -c config.c


ttyreader.o: ttyreader.c aprsg.h
	$(CC) $(CFLAGS) -c ttyreader.c

ax25.o: ax25.c aprsg.h
	$(CC) $(CFLAGS) -c ax25.c

beacon.o: beacon.c aprsg.h
	$(CC) $(CFLAGS) -c beacon.c


aprsg: $(OBJS)
	$(CC) $(CFLAGS) -o aprsg $(OBJS) $(LIBS)
