#!/bin/sh

INCLUDES="-nostdlibinc"
INCLUDES="$INCLUDES -isystem /usr/local/include"
INCLUDES="$INCLUDES -isystem /usr/local/include/boost-1_50"
if [ -d /usr/local/include/c++/v1 ]; then
    INCLUDES="$INCLUDES -isystem /usr/local/include/c++/v1"
fi
INCLUDES="$INCLUDES -isystem /opt/local/include"
INCLUDES="$INCLUDES -isystem /usr/include"

if [ -f /usr/local/lib/libc++.dylib ]; then
    ./configure                                         \
        --prefix=/usr/local/stow/subconvert             \
        --with-boost-suffix=-mt                         \
        CC=clang LD=clang++ CXX=clang++                 \
        CXXFLAGS="-O3 -std=c++11 -stdlib=libc++"        \
        CPPFLAGS="-nostdlibinc $INCLUDES"               \
        LDFLAGS="-O3 -stdlib=libc++ -L/usr/local/lib /usr/local/lib/libc++.dylib"
else
    ./configure                                         \
        --enable-pch                                    \
        --prefix=/usr/local/stow/subconvert             \
        CC=clang LD=clang++ CXX=clang++                 \
        CXXFLAGS="-g -DASSERTS -std=c++11"              \
        CPPFLAGS="$INCLUDES"                            \
        LDFLAGS="-g -L/usr/local/lib -L/opt/local/lib"
fi
