#!/bin/sh

# see if we are in the top of the tree
if [ ! -f configure.in ]; then
  cd ../..
  if [ ! -f configure.in ]; then
    echo "please run this script from the base of the VICE directory"
    exit 1
  fi
fi

curdir=`pwd`

build_i386=no
build_ppc=no

if test x"$1" = "xi386" -o x"$2" = "xi386"; then
  build_i386=yes
fi

if test x"$1" = "xppc" -o x"$2" = "xppc"; then
  build_ppc=yes
fi

if test x"$build_i386" = "xno" -a x"$build_ppc" = "xno"; then
  echo "At least one platform needs to be selected for the build"
  echo "Usage: build.sh [i386] [ppc]"
  exit 1
fi

makedone=no
single_build=no

mkdir bins

if test x"$build_i386" = "xyes"; then
  CFLAGS="-s -arch i386" ./configure -v --host=i386-apple-rhapsody --prefix=/usr/local -disable-nls --without-resid
  make
  makedone=yes
  if [ ! -e src/x64 -o ! -e src/x64dtv -o ! -e src/x128 -o ! -e src/xvic -o ! -e src/xpet -o ! -e src/xplus4 -o ! -e src/xcbm2 -o ! -e src/c1541 -o ! -e src/petcat -o ! -e src/cartconv ]
  then
    echo Error: One (or more) i386 binaries missing
    exit 1
  fi
  if test x"$build_ppc" = "xyes"; then
    for i in x128 x64 x64dtv xcbm2 xpet xplus4 xvic c1541 cartconv petcat
    do
      mv src/$i bins/$i.i386
    done
  else
    single_build=yes
  fi
fi

if test x"$build_ppc" = "xyes"; then
  if test x"$makedone" = "xyes"; then
    make clean
  fi
  CFLAGS="-s -arch ppc" ./configure -v --host=ppc-apple-rhapsody --prefix=/usr/local -disable-nls --without-resid
  make
  if [ ! -e src/x64 -o ! -e src/x64dtv -o ! -e src/x128 -o ! -e src/xvic -o ! -e src/xpet -o ! -e src/xplus4 -o ! -e src/xcbm2 -o ! -e src/c1541 -o ! -e src/petcat -o ! -e src/cartconv ]
  then
    echo Error: One (or more) ppc binaries missing
    exit 1
  fi
  if test x"$build_i386" = "xyes"; then
    for i in x128 x64 x64dtv xcbm2 xpet xplus4 xvic c1541 cartconv petcat
    do
      mv src/$i bins/$i.ppc
    done
  else
    single_build=yes
  fi
fi

if test x"$single_build" = "xno"; then

  for i in x128 x64 x64dtv xcbm2 xpet xplus4 xvic c1541 cartconv petcat
  do
    lipo bins/$i.i386 bins/$i.ppc -output src/$i
  done
fi
echo "now do a 'gnumake -e prefix=$curdir/VICE-$VICEVERSION/usr/local  VICEDIR=$curdir/VICE-$VICEVERSION/usr/local/lib/vice install'"
echo "and then a 'make bindistzip' to build the package."
