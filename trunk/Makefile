#
#  APRX -- 2nd generation receive-only APRS-i-gate with
#          minimal requirement of esoteric facilities or
#          libraries of any kind beyond UNIX system libc.
#

# Expect GNU make!
VERSION=$(shell cat VERSION)
SVNVERSION=$(shell if [ -x /usr/bin/svnversion ] ; then /usr/bin/svnversion ; else echo ""; fi)
DATE=	$(shell date +"%Y %B %d")

# Directory where aprx.state, and aprx.pid -files live.
VARRUN=	/var/run
VARLOG=	/var/log
CFGFILE= /etc/aprx.conf

DEFS=	 -DAPRXVERSION="\"${VERSION}\"" -DVARRUN="\"${VARRUN}\""	\
	 -DVARLOG="\"${VARLOG}\"" -DCFGFILE="\"${CFGFILE}\""

CC=	gcc 
CFLAGS=	-g -O3  -Wall $(DEFS)


SBINDIR=/usr/sbin/
MANDIR=/usr/share/man/
CFGDIR=/etc/

LIBS=	# Nothing special needed!
HDRS=		aprx.h
SRC=		aprx.c ttyreader.c ax25.c aprsis.c beacon.c config.c netax25.c erlang.c aprxpolls.c Makefile
OBJSAPRX=	aprx.o ttyreader.o ax25.o aprsis.o beacon.o config.o netax25.o erlang.o aprxpolls.o
OBJSSTAT=	erlang.o aprx-stat.o aprxpolls.o

all:  aprx aprx-stat html pdf aprx.conf

install: all
	install -c -m 755 aprx $(SBINDIR)
	install -c -m 755 aprx-stat $(SBINDIR)
	install -c -m 644 aprx.8 $(MANDIR)/man8/
	install -c -m 644 aprx-stat.8 $(MANDIR)/man8/
	: install -c -m 644 aprx.conf $(CFGDIR)

clean:
	rm -f *~ *.o aprx aprx-stat *.ps *.8 *.html *.pdf

aprx.o: aprx.c aprx.h Makefile
	$(CC) $(CFLAGS) -c aprx.c

aprsis.o: aprsis.c aprx.h Makefile
	$(CC) $(CFLAGS) -c aprsis.c

aprxpolls.o: aprxpolls.c aprx.h Makefile
	$(CC) $(CFLAGS) -c aprxpolls.c

config.o: config.c aprx.h Makefile
	$(CC) $(CFLAGS) -c config.c

erlang.o: erlang.c aprx.h Makefile
	$(CC) $(CFLAGS) -c erlang.c $(ERLANG)

ttyreader.o: ttyreader.c aprx.h Makefile
	$(CC) $(CFLAGS) -c ttyreader.c

ax25.o: ax25.c aprx.h Makefile
	$(CC) $(CFLAGS) -c ax25.c

netax25.o: netax25.c aprx.h Makefile
	$(CC) $(CFLAGS) -c netax25.c

beacon.o: beacon.c aprx.h Makefile
	$(CC) $(CFLAGS) -c beacon.c

aprx-stat.o: aprx-stat.c aprx.h Makefile
	$(CC) $(CFLAGS) -c aprx-stat.c

aprx: $(OBJSAPRX)
	$(CC) $(CFLAGS) -o aprx $(OBJSAPRX) $(LIBS)

aprx-stat: $(OBJSSTAT)
	$(CC) $(CFLAGS) -o aprx-stat $(OBJSSTAT) $(LIBS)

pdf: aprx.8.pdf aprx-stat.8.pdf
html: aprx.8.html aprx-stat.8.html

aprx.8.html: aprx.8
	sh man-to-html.sh aprx.8 > aprx.8.html

aprx-stat.8.html: aprx-stat.8
	sh man-to-html.sh aprx-stat.8 > aprx-stat.8.html

aprx.8.pdf: aprx.8
	groff -man aprx.8 > aprx.8.ps
	ps2pdf aprx.8.ps
	rm -f aprx.8.ps

aprx-stat.8.pdf: aprx-stat.8
	groff -man aprx-stat.8 > aprx-stat.8.ps
	ps2pdf aprx-stat.8.ps
	rm -f aprx-stat.8.ps

aprx.8: aprx.8.in Makefile
	perl -ne "s{\@DATEVERSION\@}{${VERSION} - ${DATE}}g;		\
	          s{\@VARRUN\@}{${VARRUN}}g;				\
	          s{\@VARLOG\@}{${VARLOG}}g;				\
	          s{\@CFGFILE\@}{${CFGFILE}}g;				\
		  print;"						\
	 < aprx.8.in > aprx.8

aprx-stat.8: aprx-stat.8.in Makefile
	perl -ne "s{\@DATEVERSION\@}{${VERSION} - ${DATE}}g;		\
	          s{\@VARRUN\@}{${VARRUN}}g;				\
	          s{\@VARLOG\@}{${VARLOG}}g;				\
	          s{\@CFGFILE\@}{${CFGFILE}}g;				\
		  print;"						\
	 < aprx-stat.8.in > aprx-stat.8

aprx.conf: aprx.conf.in Makefile
	perl -ne "s{\@DATEVERSION\@}{${VERSION} - ${DATE}}g;		\
	          s{\@VARRUN\@}{${VARRUN}}g;				\
	          s{\@VARLOG\@}{${VARLOG}}g;				\
	          s{\@CFGFILE\@}{${CFGFILE}}g;				\
		  print;"						\
	 < aprx.conf.in > aprx.conf

dist:
	# Special for OH2MQK only..
	if [ ! -d ../../${VERSION} ] ; then mkdir ../../${VERSION} ; fi
	cp -p * ../../${VERSION}/
	cd ../../${VERSION} && make clean
	cd ../.. && tar czvf ${VERSION}.tar.gz ${VERSION}
