#!/bin/bash
#
# This script builds mod_ssl.so for Apache 2.2.x, with SSL NPN
# support.
#
# Since NPN is still very new, it's not included in any currenly
# available release of OpenSSL. It is slated to be included in the
# 1.0.1a release.
#
# Likewise, NPN is not yet supported in Apache HTTPD mod_ssl. A patch
# has been submitted to Apache to enable NPN in mod_ssl:
# https://issues.apache.org/bugzilla/show_bug.cgi?id=52210
#
# Thus, we download the most recent releases of OpenSSL and Apache
# 2.2, and apply patches to enable NPN support in each. The OpenSSL
# NPN patch is described here:
# http://technotes.googlecode.com/git/nextprotoneg.html
#
# We currently statically link OpenSSL with mod_ssl, which results in
# a large (several megabyte) mod_ssl.so. If you prefer, you can
# install NPN-enabled OpenSSL as a shared library system-wide, by
# building OpenSSL like so (after applying the NPN patch):
#
# ./config shared -fPIC  # -fPIC is only needed on some architectures
# make
# sudo make install
#
# And Apache like so (after applying the NPN patch):
#
# ./configure --enable-ssl=shared
# make

MODSSL_SO_DESTPATH=$(pwd)/mod_ssl.so

if [ -f $MODSSL_SO_DESTPATH ]; then
echo "mod_ssl already exists at $MODSSL_SO_DESTPATH. Please remove."
exit 1
fi

BUILDROOT=$(mktemp -d)

function do_cleanup {
echo ""
echo "Build aborted."
echo -n "Cleaning up ... "
rm -rf "$BUILDROOT"
echo "done"
exit 1
}

trap 'do_cleanup' SIGINT SIGTERM

function download_file {
echo "Downloading $1"
curl -# "$1" -o $2 || do_cleanup
if [[ $(md5sum $2 | cut -d\  -f1) != $3 ]]; then
  echo "md5sum mismatch for $2"
  do_cleanup
fi
}

function uncompress_file {
echo -n "Uncompressing $1 ... "
tar xzf $1 || do_cleanup
echo "done"
}

OPENSSL_SRC_TGZ_URL="http://www.openssl.org/source/openssl-1.0.0e.tar.gz"
OPENSSL_NPN_PATCH_URL="http://technotes.googlecode.com/git/openssl-1.0.0d-npn.patch"
APACHE_HTTPD_SRC_TGZ_URL="http://www.apache.org/dist/httpd/httpd-2.2.21.tar.gz"
APACHE_HTTPD_MODSSL_NPN_PATCH_URL="https://issues.apache.org/bugzilla/attachment.cgi?id=27969&action=diff&context=patch&collapsed=&headers=1&format=raw"

OPENSSL_SRC_TGZ=$(basename $OPENSSL_SRC_TGZ_URL)
OPENSSL_NPN_PATCH=$(basename $OPENSSL_NPN_PATCH_URL)
APACHE_HTTPD_SRC_TGZ=$(basename $APACHE_HTTPD_SRC_TGZ_URL)
APACHE_HTTPD_MODSSL_NPN_PATCH="mod_ssl_npn.patch"
APACHE_HTTPD_MODSSL_NPN_PATCH_VERSION_FIX="mod_ssl_npn.patch.versionfix"

OPENSSL_SRC_ROOT=${OPENSSL_SRC_TGZ%.tar.gz}
APACHE_HTTPD_SRC_ROOT=${APACHE_HTTPD_SRC_TGZ%.tar.gz}

pushd $BUILDROOT >/dev/null

download_file $OPENSSL_SRC_TGZ_URL $OPENSSL_SRC_TGZ 7040b89c4c58c7a1016c0dfa6e821c86
download_file $OPENSSL_NPN_PATCH_URL $OPENSSL_NPN_PATCH edf66794cd005cbc3e05cf22f7326967
download_file $APACHE_HTTPD_SRC_TGZ_URL $APACHE_HTTPD_SRC_TGZ b24ca6db942a4f8e57c357e5e3058d31
download_file $APACHE_HTTPD_MODSSL_NPN_PATCH_URL $APACHE_HTTPD_MODSSL_NPN_PATCH cfd98e0b0b13f86825df7610b2437e5a

echo ""

OPENSSL_BUILDLOG=$(mktemp -p /tmp openssl_buildlog.XXXXXXXXXX)

uncompress_file $OPENSSL_SRC_TGZ

pushd $OPENSSL_SRC_ROOT >/dev/null
echo -n "Applying OpenSSL NPN patch ... "
patch -p1 <$BUILDROOT/$OPENSSL_NPN_PATCH > $OPENSSL_BUILDLOG
if [ $? -ne 0 ]; then
echo "Failed. Build log at $OPENSSL_BUILDLOG."
do_cleanup
fi
echo "done"

echo -n "Configuring OpenSSL ... "
./config no-shared -fPIC >> $OPENSSL_BUILDLOG
if [ $? -ne 0 ]; then
echo "Failed. Build log at $OPENSSL_BUILDLOG."
do_cleanup
fi
echo "done"

echo -n "Building OpenSSL (this may take a while) ... "
make INSTALL_PREFIX=$(pwd) install >> $OPENSSL_BUILDLOG 2>&1
if [ $? -ne 0 ]; then
echo "Failed. Build log at $OPENSSL_BUILDLOG."
do_cleanup
fi
echo "done"
popd >/dev/null

rm -f "$OPENSSL_BUILDLOG"

echo ""

APACHE_HTTPD_BUILDLOG=$(mktemp -p /tmp httpd_buildlog.XXXXXXXXXX)

uncompress_file $APACHE_HTTPD_SRC_TGZ

# The provided mod_ssl patch expects OpenSSL version 1.0.1, since
# that's the first OpenSSL version that will officially include NPN
# support. However, since we're patching OpenSSL version 1.0.0e, we
# need to change the version check to look for this version instead.
echo -n "Updating Apache mod_ssl minimum OpenSSL version check ... "
sed 's/0x10001000L/0x1000005fL/' $APACHE_HTTPD_MODSSL_NPN_PATCH > $APACHE_HTTPD_MODSSL_NPN_PATCH_VERSION_FIX
if [ $? -ne 0 ]; then
echo "Failed. Build log at $APACHE_HTTPD_BUILDLOG."
do_cleanup
fi
echo "done"

pushd $APACHE_HTTPD_SRC_ROOT >/dev/null
echo -n "Applying Apache mod_ssl NPN patch ... "
patch -p0 <$BUILDROOT/$APACHE_HTTPD_MODSSL_NPN_PATCH_VERSION_FIX >> $APACHE_HTTPD_BUILDLOG
if [ $? -ne 0 ]; then
echo "Failed. Build log at $APACHE_HTTPD_BUILDLOG."
do_cleanup
fi
echo "done"

echo -n "Configuring Apache mod_ssl ... "
./configure --enable-ssl=shared --with-ssl=$BUILDROOT/$OPENSSL_SRC_ROOT/usr/local/ssl >> $APACHE_HTTPD_BUILDLOG
if [ $? -ne 0 ]; then
echo "Failed. Build log at $APACHE_HTTPD_BUILDLOG."
do_cleanup
fi
echo "done"

echo -n "Building Apache mod_ssl (this may take a while) ... "
make >> $APACHE_HTTPD_BUILDLOG 2>&1
if [ $? -ne 0 ]; then
echo "Failed. Build log at $APACHE_HTTPD_BUILDLOG."
do_cleanup
fi
echo "done"
popd >/dev/null

#rm -f "$APACHE_HTTPD_BUILDLOG"

popd >/dev/null

MODSSL_SO_SRCPATH=$(find $BUILDROOT/$APACHE_HTTPD_SRC_ROOT -name mod_ssl.so)
if [ $(echo $MODSSL_SO_SRCPATH | wc -l) -ne 1 ]; then
echo "Found multiple mod_ssl.so's:"
echo $MODSSL_SO_SRCPATH
do_cleanup
fi

cp $MODSSL_SO_SRCPATH $MODSSL_SO_DESTPATH

#rm -rf "$BUILDROOT"

echo ""
echo "Generated mod_ssl.so at $MODSSL_SO_DESTPATH."
