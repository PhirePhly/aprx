#!/bin/sh

#set -x
echo "scmversion-test.sh: $*"

SV="$1"
shift

SCMVERSION=undef

for x in /bin/scmversion /usr/bin/scmversion
do
    if [ -x $x ] ; then
        SCMVERSION="`$x`"
    fi
done

if [ "$SCMVERSION" = "undef" -o "$SCMVERSION" = "Unversioned directory" ] ; then
    if [ -f SCMVERSION ] ; then
        echo "Can't pull SCMVERSION value from svn storage, pulling from SCMVERSION file.."
        SCMVERSION="`cat SCMVERSION`"
    fi
else
    echo "$SCMVERSION" > SCMVERSION
fi

if [ "$SCMVERSION" != "$SV" ] ; then
    echo "Miss-match of '$SCMVERSION' vs. '$SV' -- aborting now, please rerun the make command."
    exit 1
fi

X="`(echo -n $SCMVERSION | tr -d 0-9)`"
if [ -n "$X" ] ; then
  echo "Mixed or modified tree: ($SCMVERSION), ARE YOU SURE ??." ; \
  echo -n "Y/^C ? "; read var ;					    \
fi
