#!/bin/sh

#set -x
echo "svnversion-test.sh: $*"

SV="$1"
shift

SVNVERSION=undef

for x in /bin/svnversion /usr/bin/svnversion
do
    if [ -x $x ] ; then
        SVNVERSION="`$x`"
    fi
done

if [ "$SVNVERSION" = "undef" -o "$SVNVERSION" = "Unversioned directory" ] ; then
    if [ -f SVNVERSION ] ; then
        echo "Can't pull SVNVERSION value from svn storage, pulling from SVNVERSION file.."
        SVNVERSION="`cat SVNVERSION`"
    fi
else
    echo "$SVNVERSION" > SVNVERSION
fi

if [ "$SVNVERSION" != "$SV" ] ; then
    echo "Miss-match of '$SVNVERSION' vs. '$SV' -- aborting now, please rerun the make command."
    exit 1
fi

X="`(echo -n $SVNVERSION | tr -d 0-9)`"
if [ -n "$X" ] ; then
  echo "Mixed or modified tree: ($SVNVERSION), ARE YOU SURE ??." ; \
  echo -n "Y/^C ? "; read var ;					    \
fi
