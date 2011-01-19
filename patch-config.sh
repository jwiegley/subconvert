#!/bin/zsh
perl -i -pe 's%url = git://github.com/boost-lib%url = git\@github.com:boost-lib%;' \
    $1/**/.git/config
