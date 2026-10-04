#!/bin/sh
#
# gentoo-setup.sh [PACKAGE...] -- install, into a Gentoo stage3 container,
# what building and testing the plugin needs, plus any PACKAGEs named.
#
# Most of it comes prebuilt from Gentoo's binary package host.  TDLib is not
# there, and libpurple is wanted without its GUI, so those two are built
# here -- and kept in /var/cache/binpkgs, which the workflow caches between
# runs, so that only a new TDLib release is built again.

set -e

export MAKEOPTS="-j$(nproc)"

emerge-webrsync --quiet

mkdir -p /etc/portage/package.accept_keywords /etc/portage/package.use
# TDLib is only ever keyworded ~arch.
echo 'net-libs/tdlib' > /etc/portage/package.accept_keywords/tdlib
echo 'net-im/pidgin -gui -gstreamer -xscreensaver' > /etc/portage/package.use/pidgin

# Index whatever the cache brought back.
if [ -d /var/cache/binpkgs ]; then
	emaint binhost --fix
fi

emerge --quiet-build --noreplace --getbinpkg --usepkg --buildpkg \
    dev-build/cmake dev-build/ninja dev-cpp/gtest dev-libs/libfmt \
    media-libs/libpng media-libs/libwebp media-libs/rlottie \
    net-im/pidgin net-libs/tdlib \
    "$@"
