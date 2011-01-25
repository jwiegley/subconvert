#!/bin/bash

set -o errexit

TOP=/Volumes/RAID/Mirrors/boost

BOOST_RYPPL=$TOP/ryppl
BOOST_SVN=$TOP/boost-svn
BOOST_GIT=$TOP/boost-git

if [[ $1 == "reset" ]]; then
    (cd $BOOST_GIT && \
     git checkout master && \
     git reset --hard origin/master && \
     git submodule foreach git checkout master && \
     git submodule foreach git reset --hard origin/master && \
     git clean -fdx && \
     git status --porcelain | awk '{print $2}' | xargs rm -fr)
else
    (cd $BOOST_RYPPL/boost && \
     python modularize.py -a --src=$BOOST_SVN --dst=$BOOST_GIT "$@")
fi    
