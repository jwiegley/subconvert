#!/bin/bash

LOG=$HOME/Desktop/boost-update.log

set -o errexit

MIGRATE=$HOME/src/boost-migrate
SOURCE=$HOME/src/boost

$MIGRATE/modules.sh reset > /dev/null 2>&1

cd $SOURCE

(cd boost-git; git pull; git submodule update --init) > $LOG 2>&1
(cd boost-private; git pull) >> $LOG 2>&1
(cd boost-release; git remote update) >> $LOG 2>&1
(cd boost-svn; git pull) >> $LOG 2>&1

svnsync --non-interactive sync file://$SOURCE/boost.svnrepo >> $LOG 2>&1

perl -i -pe "s%url =.*%url = file://$SOURCE/boost.svnrepo%;" boost-clone/.git/config
(cd boost-clone; git svn fetch; git svn rebase) >> $LOG 2>&1

#svnadmin dump boost.svnrepo > boost.svnrepo.dump 2> boost.svnrepo.log

$MIGRATE/modules.sh update >> $LOG 2>&1
$MIGRATE/modules.sh push >> $LOG 2>&1

mv $LOG $SOURCE
