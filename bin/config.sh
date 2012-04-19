#!/bin/sh

./configure                                     \
    --enable-pch                                \
    --prefix=/usr/local/stow/subconvert         \
    CC=clang-mp-3.1                             \
    LD=clang++-mp-3.1                           \
    CXX=clang++-mp-3.1                          \
    CXXFLAGS="-g2 -ggdb"                        \
    CPPFLAGS="-isystem /opt/local/include"      \
    LDFLAGS=-L/opt/local/lib
