#!/bin/bash

set -e

export PATH=$PATH:~/src/cov-analysis-linux64-6.5.1/bin

make clean
./configure
rm -rf cov-int
cov-build --dir cov-int make
tar cvfz aprx-coverity.tgz cov-int
rm -rf cov-int

VERSION="`cat VERSION`"
PROJECT="Aprx"
PASSWORD="`cat ~/.covpw`"

echo "Uploading Aprx version $VERSION to Coverity..."

curl --form file=@aprx-coverity.tgz --form project="$PROJECT" \
	--form password="$PASSWORD" \
	--form email=oh2mqk@sral.fi \
	--form version="$VERSION" \
	--form description="" \
	http://scan5.coverity.com/cgi-bin/upload.py

rm -f aprx-coverity.tgz
