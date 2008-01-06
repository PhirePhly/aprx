#
#  APRX -- 2nd generation receive-only APRS-i-gate with
#          minimal requirement of esoteric facilities or
#          libraries of any kind beyond UNIX system libc.
#
# Note: This makefile uses features from GNU make

# -------------------------------------------------------------------- #

# target paths
VARRUN=		/var/run	# directory for aprx.state and pid-file
VARLOG=		/var/log	# directory for direct logfiles
CFGFILE=	/etc/aprx.conf	# default configuration file
SBINDIR=	/usr/sbin/	# installation path for programs
MANDIR=		/usr/share/man	# installation path for manual pages

# -------------------------------------------------------------------- #

# Compiler and flags
CC=		gcc 
CFLAGS=		-g -O3 -Wall $(DEFS)

# Linker and flags
LD=		$(CC)
LDFLAGS=	-g -Wall

INSTALL=	/usr/bin/install
INSTALL_PROGRAM=$(INSTALL)  -m 755
INSTALL_DATA=	$(INSTALL)  -m 644

# -------------------------------------------------------------------- #
# no user serviceable parts below 
# -------------------------------------------------------------------- #

# strip extra whitespace from paths
VARRUN:=$(strip $(VARRUN))
VARLOG:=$(strip $(VARLOG))
CFGFILE:=$(strip $(CFGFILE))
SBINDIR:=$(strip $(SBINDIR))
MANDIR:=$(strip $(MANDIR))

# generate version strings
VERSION:=$(shell cat VERSION)
SVNVERSION:=$(shell if [ -x /usr/bin/svnversion ] ; then /usr/bin/svnversion ; else echo "0"; fi)
DATE:=$(shell date +"%Y %B %d")

DEFS=	 -DAPRXVERSION="\"$(VERSION)\"" -DVARRUN="\"$(VARRUN)\"" \
	 -DVARLOG="\"$(VARLOG)\"" -DCFGFILE="\"$(CFGFILE)\""

# program names
PROGAPRX=	aprx
PROGSTAT=	$(PROGAPRX)-stat

LIBS=	# Nothing special needed!
OBJSAPRX=	aprx.o ttyreader.o ax25.o aprsis.o beacon.o config.o	\
		netax25.o erlang.o aprxpolls.o
OBJSSTAT=	erlang.o aprx-stat.o aprxpolls.o

# man page sources, will be installed as $(PROGAPRX).8 / $(PROGSTAT).8
MANAPRX := 	aprx.8
MANSTAT := 	aprx-stat.8

OBJS=		$(OBJSAPRX) $(OBJSSTAT)
MAN=		$(MANAPRX) $(MANSTAT)

# -------------------------------------------------------------------- #

.PHONY: 	all
all:		$(PROGAPRX) $(PROGSTAT) man aprx.conf

$(PROGAPRX):	$(OBJSAPRX) VERSION Makefile
		$(LD) $(LDLAGS) -o $@ $(OBJSAPRX) $(LIBS)

$(PROGSTAT):	$(OBJSSTAT) VERSION Makefile
		$(LD) $(LDLAGS) -o $@ $(OBJSSTAT) $(LIBS)

.PHONY:		man
man:		$(MAN)

.PHONY:		doc html pdf
doc:		html pdf
pdf:		$(MAN:=.pdf)
html:		$(MAN:=.html)

# -------------------------------------------------------------------- #

.PHONY:	install
install: all
	$(INSTALL_PROGRAM) $(PROGAPRX) $(DESTDIR)$(SBINDIR)/$(PROGAPRX)
	$(INSTALL_PROGRAM) $(PROGSTAT) $(DESTDIR)$(SBINDIR)/$(PROGSTAT)
	$(INSTALL_DATA) $(MANAPRX) $(DESTDIR)$(MANDIR)/man8/$(PROGAPRX).8
	$(INSTALL_DATA) $(MANSTAT) $(DESTDIR)$(MANDIR)/man8/$(PROGSTAT).8
	if [ ! -f  $(DESTDIR)$(CFGFILE) ] ; then \
		$(INSTALL_DATA) aprx.conf $(DESTDIR)$(CFGFILE) ; \
	else true ; fi

.PHONY: clean
clean:
	rm -f $(PROGAPRX) $(PROGSTAT)	\
	      $(MAN) $(MAN:=.html) $(MAN:=.ps) $(MAN:=.pdf)	\
	      aprx.conf	\
	      $(OBJS)	\
	      $(OBJS:.o=.d)

.PHONY: distclean
distclean: clean
	rm -f *~ *.o *.d

# -------------------------------------------------------------------- #

# include object depencies if available
-include $(OBJS:.o=.d)

%.o: %.c VERSION Makefile
	$(CC) $(CFLAGS) -c $<
	@$(CC) -MM $(CFLAGS) $< > $(@:.o=.d)

$(MAN:=.html): %.html : %
	sh man-to-html.sh $< > $@

$(MAN:=.ps): %.ps : %
	groff -man $< > $@

$(MAN:=.pdf): %.pdf : %.ps
	ps2pdf $<

$(MAN)	\
aprx.conf: % : %.in VERSION Makefile
	perl -ne "s{\@DATEVERSION\@}{$(VERSION) - $(DATE)}g;	\
	          s{\@VARRUN\@}{$(VARRUN)}g;			\
	          s{\@VARLOG\@}{$(VARLOG)}g;			\
	          s{\@CFGFILE\@}{$(CFGFILE)}g;			\
		  print;"					\
	 < $< > $@

# -------------------------------------------------------------------- #

.PHONY: dist
dist:
	# Special for maintainer only..
	if [ ! -d ../../$(VERSION)-svn$(SVNVERSION) ] ;		\
	then							\
		mkdir ../../$(VERSION)-svn$(SVNVERSION) ;	\
	fi
	cp -p * ../../$(VERSION)-svn$(SVNVERSION)/
	echo "$(VERSION)-svn$(SVNVERSION)" > ../../$(VERSION)-svn$(SVNVERSION)/VERSION
	cd ../../$(VERSION)-svn$(SVNVERSION)/ && make distclean
	cd ../.. && 	\
	tar czvf $(VERSION)-svn$(SVNVERSION).tar.gz $(VERSION)-svn$(SVNVERSION)

# -------------------------------------------------------------------- #
