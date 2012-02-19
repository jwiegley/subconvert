#!/bin/bash

LOG=$HOME/Library/Logs/boost-update.log

#RAMDISK=/tmp/ramdisk
RAMDISK=$HOME/Products

set -e

BOOST=/Volumes/Data/Mirrors/Boost
MIGRATE=$HOME/Contracts/BoostPro/Projects/boost-migrate

$MIGRATE/bin/modules.sh reset > $LOG 2>&1

cd $BOOST

(cd boost-git; git pull; git submodule update --init) >> $LOG 2>&1
(cd boost-private; git pull) >> $LOG 2>&1
(cd boost-svn; git pull) >> $LOG 2>&1
(cd Boost.Defrag; git pull) >> $LOG 2>&1
(cd installer; git pull) >> $LOG 2>&1
(cd ryppl; git pull) >> $LOG 2>&1
(cd boost-modularize; git pull) >> $LOG 2>&1

svnsync --non-interactive sync file://$PWD/boost.svnrepo >> $LOG 2>&1
perl -i -pe "s%url =.*%url = file://$PWD/boost.svnrepo%;" boost-clone/.git/config
(cd boost-clone; git svn fetch; git reset --hard trunk) >> $LOG 2>&1

$MIGRATE/bin/modules.sh update >> $LOG 2>&1
#$MIGRATE/bin/modules.sh push >> $LOG 2>&1

time svnadmin dump -q boost.svnrepo | xz -9ec > boost.svnrepo.dump.xz 2>> $LOG

#if [[ ! -d $RAMDISK/cpp ]]; then
#    if [[ ! -d $RAMDISK ]]; then
#        mkramdisk
#        mkramdisk
#    fi
#    mkdir $RAMDISK/cpp
#fi

if [[ -d $RAMDISK/cpp ]]; then
    /bin/rm -fr $RAMDISK/cpp
    mkdir $RAMDISK/cpp
    cd $RAMDISK/cpp

    git init >> $LOG 2>&1
    $MIGRATE/subconvert -A $MIGRATE/doc/authors.txt -B $MIGRATE/doc/branches.txt \
        convert $BOOST//boost.svnrepo.dump >> $LOG 2>&1
    git symbolic-ref HEAD refs/heads/trunk >> $LOG 2>&1
    git checkout trunk >> $LOG 2>&1
    git gc >> $LOG 2>&1
    git remote add origin git@github.com:boost-lib/boost-history.git >> $LOG 2>&1
    sleep 300
    git push -f --all origin >> $LOG 2>&1
    git push -f --mirror origin >> $LOG 2>&1
    git push -f --tags origin >> $LOG 2>&1
fi

mv $LOG $BOOST
