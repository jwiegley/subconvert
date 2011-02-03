#!/bin/bash

LOG=$HOME/Desktop/boost-update.log
RAMDISK=/tmp/ramdisk

set -o errexit

MIGRATE=$HOME/src/boost-migrate
SOURCE=$HOME/src/boost

$MIGRATE/bin/modules.sh reset > /dev/null 2>&1

cd $SOURCE

(cd boost-git; git pull; git submodule update --init) > $LOG 2>&1
(cd boost-private; git pull) >> $LOG 2>&1
(cd boost-release; git remote update) >> $LOG 2>&1
(cd boost-svn; git pull) >> $LOG 2>&1

svnsync --non-interactive sync file://$SOURCE/boost.svnrepo >> $LOG 2>&1

perl -i -pe "s%url =.*%url = file://$SOURCE/boost.svnrepo%;" boost-clone/.git/config
(cd boost-clone; git svn fetch; git svn rebase) >> $LOG 2>&1

$MIGRATE/bin/modules.sh update >> $LOG 2>&1
$MIGRATE/bin/modules.sh push >> $LOG 2>&1

svnadmin dump boost.svnrepo > boost.svnrepo.dump 2>> $LOG

if [[ -d $RAMDISK/cpp ]]; then
    /bin/rm -fr $RAMDISK/cpp
    mkdir $RAMDISK/cpp
    cd $RAMDISK/cpp
    git init >> $LOG 2>&1
    $MIGRATE/subconvert -A $MIGRATE/doc/authors.txt -B $MIGRATE/doc/branches.txt \
        convert $SOURCE//boost.svnrepo.dump >> $LOG 2>&1
    git symbolic-ref HEAD refs/heads/trunk >> $LOG 2>&1
    git checkout trunk >> $LOG 2>&1
    git gc >> $LOG 2>&1
    git remote add origin git@github.com:boost-lib/boost-history.git >> $LOG 2>&1
    git push -f --all origin >> $LOG 2>&1
    git push -f --mirror origin >> $LOG 2>&1
    git push -f --tags origin >> $LOG 2>&1
fi

mv $LOG $SOURCE
