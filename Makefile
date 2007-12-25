#
#  APRX -- 2nd generation receive-only APRS-i-gate with
#          minimal requirement of esoteric facilities or
#          libraries of any kind beyond UNIX system libc.
#

# easy way to have 1 minute ERLANG data included:  make ERLANG1=1
ifdef ERLANG1
ERLANG="-DUSE_ONE_MINUTE_INTERVAL=1"
endif

CC=	gcc 
CFLAGS=	-g -O3  -Wall

SBINDIR=/usr/sbin/
MANDIR=/usr/share/man/
CFGDIR=/etc/

LIBS=	# Nothing special needed!
HDRS=	aprx.h
SRC=		aprx.c ttyreader.c ax25.c aprsis.c beacon.c config.c netax25.c erlang.c
OBJSAPRX=	aprx.o ttyreader.o ax25.o aprsis.o beacon.o config.o netax25.o erlang.o
OBJSSTAT=	erlang.o aprx-stat.o

all:  aprx aprx-stat

install:
	install -c -m 755 aprx $(SBINDIR)
	install -c -m 644 aprx.conf $(CFGDIR)
	install -c -m 644 aprx.8 $(MANDIR)/man8/
	install -c -m 644 aprx-stat.8 $(MANDIR)/man8/

clean:
	rm -f *~ *.o aprx aprx-stat *.ps 


aprx.o: aprx.c aprx.h
	$(CC) $(CFLAGS) -c aprx.c

aprsis.o: aprsis.c aprx.h
	$(CC) $(CFLAGS) -c aprsis.c

config.o: config.c aprx.h
	$(CC) $(CFLAGS) -c config.c

erlang.o: erlang.c aprx.h
	$(CC) $(CFLAGS) -c erlang.c $(ERLANG)

ttyreader.o: ttyreader.c aprx.h
	$(CC) $(CFLAGS) -c ttyreader.c

ax25.o: ax25.c aprx.h
	$(CC) $(CFLAGS) -c ax25.c

netax25.o: netax25.c aprx.h
	$(CC) $(CFLAGS) -c netax25.c


beacon.o: beacon.c aprx.h
	$(CC) $(CFLAGS) -c beacon.c

aprx: $(OBJSAPRX)
	$(CC) $(CFLAGS) -o aprx $(OBJSAPRX) $(LIBS)

aprx-stat: $(OBJSSTAT)
	$(CC) $(CFLAGS) -o aprx-stat $(OBJSSTAT) $(LIBS)

pdf aprx.8.pdf: aprx.8 aprx-stat.8
	groff -man aprx.8 > aprx.8.ps
	ps2pdf aprx.8.ps
	rm -f aprx.8.ps
	groff -man aprx-stat.8 > aprx-stat.8.ps
	ps2pdf aprx-stat.8.ps
	rm -f aprx-stat.8.ps
