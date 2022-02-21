#!/bin/sh
#
#       Oct 29, 2002 -- swhite@sekrut.net
#         inspired by embeem's build_x_compiler.sh for PPC TiVo's
#       Mar 05, 2003 -- swhite@sekrut.net
#         updated to use binutils-2.13 due to multiple people
#         unable to build kernel/modules with binutils-2.11
#

export TARGET=mips-TiVo-linux
export PREFIX=/usr/local/mips-tivo
export PATH=${PATH}:${PREFIX}/bin

if [ ! -f binutils-2.13.tar.gz ]; then
	echo "Getting binutils-2.13"
	wget ftp://ftp.gnu.org/gnu/binutils/binutils-2.13.tar.gz
fi

if [ ! -f gcc-3.0.tar.gz ]; then
	echo "Getting gcc-3.0.tar.gz"
	wget ftp://ftp.gnu.org/gnu/gcc/gcc-3.0.tar.gz
fi

if [ ! -f glibc-2.2.3.tar.gz ]; then
	echo "Getting glibc-2.2.3.tar.gz"
	wget ftp://ftp.gnu.org/gnu/glibc/glibc-2.2.3.tar.gz
fi

if [ ! -f glibc-linuxthreads-2.2.3.tar.gz ]; then
	echo "Getting glibc-linuxthreads-2.2.3"
	wget ftp://ftp.gnu.org/gnu/glibc/glibc-linuxthreads-2.2.3.tar.gz 
fi

if [ ! -f TiVo-4.0-linux-2.4.tar.gz ]; then
	echo "Getting TiVo-4.0-linux-2.4.tar.gz"
	wget "http://tivo.com/linux/40/TiVo-4.0-linux-2.4.tar.gz"
fi

echo "**** building binutils ****"
tar xzf binutils-2.13.tar.gz
mkdir -p build-binutils && cd build-binutils
../binutils-2.13/configure --target=$TARGET --prefix=$PREFIX

make -j3 all || { echo "**** binutils compile failed"; exit; }
make -j3 install || { echo "**** binutils install failed"; exit; }
cd ..

echo "**** building gcc (stage 1) ****"
tar xzf gcc-3.0.tar.gz
mkdir -p build-gcc && cd build-gcc
../gcc-3.0/configure --target=$TARGET --prefix=$PREFIX \
--without-headers --with-newlib --disable-shared --enable-languages=c

make -j3 all-gcc || { echo "**** gcc (stage 1) compile failed"; exit; }
make -j3 install-gcc || { echo "**** gcc (stage 1) install failed"; exit; }
cd ..

echo "**** mucking with tivo kernel source ****"
tar xzf TiVo-4.0-linux-2.4.tar.gz
cd linux-2.4

## yes this is really "cheap"
## but I didn't know how else to get autoconf.h generated
mv ismdefs ismdefs.old
sed 's/^include/\# include/' < ismdefs.old > ismdefs
yes "" | make -j3 ARCH=mips CROSS_COMPILE=mips-TiVo-linux config
make -j3 ARCH=mips CROSS_COMPILE=mips-TiVo-linux include/linux/version.h

mkdir -p ${PREFIX}/${TARGET}/include
cp -r include/linux ${PREFIX}/${TARGET}/include
cp -r include/asm-mips ${PREFIX}/${TARGET}/include/asm
cd ..

echo "**** building glibc ****"
tar xzf glibc-2.2.3.tar.gz
tar xzf glibc-linuxthreads-2.2.3.tar.gz -C glibc-2.2.3
mkdir -p build-glibc && cd build-glibc
CC=mips-TiVo-linux-gcc ../glibc-2.2.3/configure \
--host=$TARGET --prefix=$PREFIX --disable-debug --disable-profile \
--enable-add-ons --with-headers=${PREFIX}/${TARGET}/include

# Expect one failure here...
make -j3
# Fix the file and recompile.
mv elf/rtld-ldscript elf/rtld-ldscript.old
sed 's/elf32-bigmips/elf32-tradbigmips/' < elf/rtld-ldscript.old > elf/rtld-ldscript
make -j3 || { echo "**** glibc compile failed"; exit; }
make -j3 install || { echo "**** glibc install failed"; exit; }
cd ..

echo "**** mucking around in ${PREFIX}/${TARGET} ****"
mv ${PREFIX}/${TARGET}/include/asm ${PREFIX}/include
mv ${PREFIX}/${TARGET}/include/linux ${PREFIX}/include
rm -r ${PREFIX}/${TARGET}/include
rm -r ${PREFIX}/${TARGET}/lib
ln -s ${PREFIX}/include ${PREFIX}/${TARGET}/include
ln -s ${PREFIX}/lib ${PREFIX}/${TARGET}/lib

echo "**** building gcc (final stage) ****"
mkdir -p build-gcc2 && cd build-gcc2
../gcc-3.0/configure --target=$TARGET --prefix=$PREFIX --enable-languages=c,c++

make -j3 all || { echo "**** gcc (final stage) build failed"; exit; }
make -j3 install || { echo "**** gcc (final stage) install failed"; exit; }
cd ..

echo "**** setting up symlinks in ${PREFIX}/bin ****"
for f in ${PREFIX}/bin/mips-TiVo-linux-*; do
	ln -s $f `echo $f | sed 's/mips-TiVo-linux-//'`
done

echo "**** completed install with no errors ****"
echo "**** Enjoy! ****"
