#!/bin/bash

set -e

error() {
    cat <<EOF | mutt -a ~/Library/Logs/boost-update.log \
        -s '[boost] Migration update FAILED' johnw@boostpro.com
Boost migration update build FAILED.
EOF
}

trap 'error ${LINENO}' ERR

exec > ~/Library/Logs/boost-update.log 2>&1

RAMDISK=/tmp/ramdisk
BOOST=/Volumes/Data/Mirrors/Boost
MIGRATE=$HOME/Contracts/BoostPro/Projects/boost-migrate

$MIGRATE/bin/modules.sh reset

cd $BOOST

(cd boost-git; git pull; git submodule update --init)
(cd boost-private; git pull)
(cd boost-svn; git pull)
(cd Boost.Defrag; git pull)
(cd installer; git pull)
(cd ryppl; git pull)
(cd boost-modularize; git pull)

svnsync --non-interactive sync file://$PWD/boost.svnrepo
svnadmin dump -q boost.svnrepo > boost.svnrepo.dump

perl -i -pe "s%url =.*%url = file://$PWD/boost.svnrepo%;" boost-clone/.git/config
(cd boost-clone; git svn fetch; git reset --hard trunk)

if [[ ! -d $RAMDISK ]]; then
    mkramdisk

    if [[ -d $RAMDISK ]]; then
        mkdir $RAMDISK/cpp
    fi
fi

if [[ -d $RAMDISK/cpp ]]; then
    /bin/rm -fr $RAMDISK/cpp
    mkdir $RAMDISK/cpp
    cd $RAMDISK/cpp

    git init
    time $MIGRATE/subconvert                                            \
        -A $MIGRATE/doc/authors.txt                                     \
        -B $MIGRATE/doc/branches.txt                                    \
        convert $BOOST//boost.svnrepo.dump
    git symbolic-ref HEAD refs/heads/trunk
    git checkout trunk
    git gc

    sleep 300

    #git remote add origin git@github.com:boost-lib/boost-history.git
    #git push -f --all origin
    #git push -f --mirror origin
    #git push -f --tags origin

    rsync -av --delete .git/ $BOOST/boost-history.git/
    cd $BOOST
    diskutil eject $RAMDISK
fi

$MIGRATE/bin/modules.sh update
#$MIGRATE/bin/modules.sh push

cat <<EOF | mutt -a ~/Library/Logs/boost-update.log \
    -s '[boost] Migration update succeeded' johnw@boostpro.com
Boost migration update build succeeded.
EOF

exit 0
