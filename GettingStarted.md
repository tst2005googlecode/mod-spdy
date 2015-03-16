**The easiest way to install mod\_spdy is to download and install one of the binary packages from https://developers.google.com/speed/spdy/mod_spdy/.  If instead you'd like to compile from source, please see the instructions below.**

To install mod\_spdy you will also need to install a version of mod\_ssl with [NPN](http://technotes.googlecode.com/git/nextprotoneg.html) support enabled. You'll also need to be familiar with building and installing software packages, and you'll need to have subversion, g++ toolchain, etc installed on your system.

Before starting, please consult our known [compatibility issues](KnownIssues.md).

## 0. Prerequesites ##

Install subversion, curl, and a gcc/g++ toolchain.

On Ubuntu:
```sh

sudo apt-get install subversion curl g++ apache2 patch binutils make devscripts```

You will also need to install the Chromium [depot tools](http://dev.chromium.org/developers/how-tos/install-depot-tools) and add them to your path.

## 1. Get the code ##

Create a directory to hold your source code.  It is important that the full directory path contains no spaces.

```sh

mkdir mod_spdy
cd mod_spdy
gclient config "http://mod-spdy.googlecode.com/svn/tags/current/src"
gclient sync --force
cd src```

## 2. Build mod\_ssl with NPN support ##

You may have a version of mod\_ssl.so on your system already. You'll need to build a more recent version from Apache and OpenSSL source and apply a patch to each to enable [NPN](http://technotes.googlecode.com/git/nextprotoneg.html) support. This is taken care of by the `build_modssl_with_npn.sh` script:

From the `mod_spdy` directory:
```sh

./build_modssl_with_npn.sh```

Wait for the build to complete.

## 3. Install mod\_ssl ##

Install mod\_ssl.so in your Apache modules directory. This may be e.g. `/usr/lib/apache2/modules` if you are using Ubuntu, or some other location depending on your system.

Instructions for Ubuntu:
```sh

sudo cp mod_ssl.so /usr/lib/apache2/modules
sudo a2enmod ssl```

## 4. Configure mod\_ssl ##

Instructions for generating a self-signed certificate on Ubuntu (fine for testing, not for production environments):
```sh

sudo apt-get install ssl-cert
make-ssl-cert /usr/share/ssl-cert/ssleay.cnf /tmp/selfsigned.crt
# Enter your hostname. Any hostname will do for testing.
sudo mkdir /etc/apache2/ssl
sudo mv /tmp/selfsigned.crt /etc/apache2/ssl
sudo emacs /etc/apache2/sites-available/default-ssl  # Any editor is fine here.
# 1. Find the line for SSLCertificateFile and update it to:  SSLCertificateFile /etc/apache2/ssl/selfsigned.crt
# 2. Comment out the line immediately following, for SSLCertificateKeyFile
sudo a2ensite default-ssl```

Alternatively, you may wish to follow [this tutorial](http://www.tc.umn.edu/~brams006/selfsign.html).

## 5. Verify mod\_ssl is working ##

Restart Apache. On Ubuntu:
```sh

sudo /etc/init.d/apache2 restart```

In any browser, navigate to e.g. https://yoursite/. If you are testing on localhost, try https://127.0.0.1/.

## 6. Build and install mod\_spdy ##

There are two ways to build and install mod\_spdy.  First, you can build a Debian or RPM package, and install it via your system's package manager.  Alternatively, you can simply build the mod\_spdy binary and install it by hand.

### Using the Debian package manager ###

If you already have mod\_spdy installed via your package manager, first remove the current version:
```sh

dpkg -r mod-spdy-beta```

From the mod\_spdy `src` directory, build and install the package:
```sh

rm -f out/Release/mod-spdy-beta*.deb
BUILDTYPE=Release make linux_package_deb
dpkg -i out/Release/mod-spdy-beta*.deb```

### Using the RPM package manager ###

If you already have mod\_spdy installed via your package manager, first remove the current version:
```sh

rpm -e mod-spdy-beta```

From the mod\_spdy `src` directory, build and install the package:
```sh

rm -f out/Release/mod-spdy-beta*.rpm
BUILDTYPE=Release make linux_package_rpm
rpm -i out/Release/mod-spdy-beta*.rpm```

### Without using a package manager ###

From the mod\_spdy `src` directory, build the binary:
```sh

make BUILDTYPE=Release```

On Ubuntu, from the `src` directory of the mod\_spdy build:

```sh

sudo cp out/Release/libmod_spdy.so /usr/lib/apache2/modules/mod_spdy.so
echo "LoadModule spdy_module /usr/lib/apache2/modules/mod_spdy.so" | sudo tee /etc/apache2/mods-available/spdy.load
echo "SpdyEnabled on" | sudo tee /etc/apache2/mods-available/spdy.conf
sudo a2enmod spdy```

## 7. Restart Apache ##

On Ubuntu:
```sh

sudo /etc/init.d/apache2 restart```

## 8. Verify in Google Chrome ##

Point your Google Chrome browser at your web site. Be sure to use an `https` URL. Open another tab and navigate to <a href='chrome://net-internals/#spdy'><code>chrome://net-internals/#spdy</code></a>, and verify that your hostname appears in the table.

**NOTE:** The SPDY protocol is designed to fall back to SSL without SPDY if things are not working correctly, so it's important that you confirm that your hostname appears in the table in the SPDY tab. If it doesn't SPDY is not working. You can also check your apache `error.log` to see if mod\_spdy was able to talk SPDY with Google Chrome.