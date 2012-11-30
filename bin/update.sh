#!/bin/bash -x

error() {
    cat <<EOF | mutt -s '[boost] Migration update FAILED' johnw@boostpro.com
Boost migration update build FAILED.
EOF
}

trap 'error ${LINENO}' ERR

RAMDISK=/tmp/ramdisk
BOOST=$HOME/Mirrors/Boost
MIGRATE="$HOME/Contracts/BoostPro/Projects/boost-migrate"

"$MIGRATE/bin/modules.sh" reset

cd $BOOST

(cd boost-git; git reset --hard HEAD; git pull; \
 git submodule foreach "git reset --hard HEAD"; \
 git submodule update --init)
(cd boost-private; git pull)
(cd boost-svn; git pull)
(cd Boost.Defrag; git pull)
#(cd installer; git pull)
(cd ryppl; git pull)
(cd boost-modularize; git pull)

svnsync --non-interactive sync file://$PWD/boost.svnrepo
#svnadmin dump -q boost.svnrepo > boost.svnrepo.dump
#pxz -9ve boost.svnrepo.dump

perl -i -pe "s%url =.*%url = file://$PWD/boost.svnrepo%;" boost-clone/.git/config
(cd boost-clone; git svn fetch; git reset --hard trunk)

exit 0

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
    if "$MIGRATE/subconvert" -q                                           \
           -A "$MIGRATE/doc/authors.txt"                                  \
           -B "$MIGRATE/doc/branches.txt"                                 \
           convert $BOOST/boost.svnrepo.dump
        git symbolic-ref HEAD refs/heads/trunk
        git prune
        sleep 5

        git remote add origin git@github.com:ryppl/boost-history.git
        git push -f --all origin
        git push -f --mirror origin
        git push -f --tags origin

        sleep 5
        rsync -av --delete .git/ $BOOST/boost-history.git/

        cd $BOOST
        sudo umount $RAMDISK
        rm -fr $RAMDISK
    fi
fi

#"$MIGRATE/bin/modules.sh" update
#"$MIGRATE/bin/modules.sh" push

cat <<EOF | mutt -s '[boost] Migration update succeeded' johnw@boostpro.com
Boost migration update succeeded.
EOF

exit 0
