#!/bin/sh

INCLUDES="-nostdlibinc"
INCLUDES="$INCLUDES -isystem /usr/local/include"
if [ -d /usr/local/include/c++/v1 ]; then
    INCLUDES="$INCLUDES -isystem /usr/local/include/c++/v1"
fi
INCLUDES="$INCLUDES -isystem /opt/local/include"
INCLUDES="$INCLUDES -isystem /usr/include"

if [ -f /usr/local/lib/libc++.dylib ]; then
    ./configure                                                         \
        --enable-pch                                                    \
        --prefix=/usr/local/stow/subconvert                             \
        --with-boost-suffix=-clang-darwin-1_49                          \
        CC=clang LD=clang++ CXX=clang++                                 \
        CXXFLAGS="-g -DASSERTS -std=c++11 -stdlib=libc++"               \
        CPPFLAGS="-nostdlibinc $INCLUDES"                               \
        LDFLAGS="-g -stdlib=libc++ -L/usr/local/lib -L/opt/local/lib /usr/local/lib/libc++.dylib"
else
    ./configure                                         \
        --enable-pch                                    \
        --prefix=/usr/local/stow/subconvert             \
        CC=clang LD=clang++ CXX=clang++                 \
        CXXFLAGS="-g -DASSERTS -std=c++11"              \
        CPPFLAGS="$INCLUDES"                            \
        LDFLAGS="-g -L/usr/local/lib -L/opt/local/lib"
fi