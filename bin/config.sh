#!/bin/sh

./configure                                                             \
    --enable-pch                                                        \
    --prefix=/usr/local/stow/subconvert                                 \
    CC=clang-mp-3.1                                                     \
    LD=clang++-mp-3.1                                                   \
    CXX=clang++-mp-3.1                                                  \
    CXXFLAGS="-g -DASSERTS"                                             \
    CPPFLAGS="-isystem /opt/local/include -isystem /usr/local/include"  \
    LDFLAGS="-g -L/opt/local/lib -L/usr/local/lib"
