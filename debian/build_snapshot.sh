#!/bin/zsh

set -e -x

# report the previous state so that we can try again
git show-ref | grep refs/heads

cd `dirname $0`/..

# Assumed:
#   git remote add upstream git://git.savannah.gnu.org/emacs.git
git fetch upstream
git reset --hard
git clean -ffdx

git merge -m 'Merging new upstream' upstream/master

gbp pq rebase
gbp pq export
git add -vf debian/patches/

# I let this fail because no patch updates may be necessary
git commit --no-verify -m 'patch update' debian/patches/ || true

# need to make this non-interactive
UPSTREAM_VER=`git describe --tags --always upstream/master`
test -n "$UPSTREAM_VER" # make sure version was parsed
export DEBEMAIL=dima@secretsauce.net
export DEBFULLNAME='Dima Kogan'
dch -v `date +'2:%Y%m%d+'$UPSTREAM_VER'-1'` 'New snapshot'
dch -r ''
git commit -m 'new snapshot' debian/changelog

# done preparing the source. Push and build
git push origin master:master

git clean -ffdx; git reset --hard
./debian/rules debian/control
./debian/rules debian/copyright

# build upstream .orig tarball if needed
gbp buildpackage --git-builder=true --git-cleaner=true --git-ignore-new

# build the package
# note that on my box the "unstable" chroot is actually "jessie"
sbuild --no-clean-source --chroot-mode=unshare --nolog -s --force-orig-source -A -d unstable


##### Done. Do it again without native compilation
git clean -ffdx; git reset --hard
(echo -e '#!/usr/bin/make -f\nNO_NATIVE_COMP:=1\n'; cat debian/rules) > debian/rules.new && mv debian/rules.new debian/rules
chmod ug+x debian/rules
perl -p -i -e 's/emacs-snapshot/emacs-snapshot-no-native-comp/g' debian/patches/0002-Run-debian-startup-and-set-debian-emacs-flavor.patch
perl -p -i -e 's/emacs-snapshot/emacs-snapshot-no-native-comp/' debian/changelog
make -f ./debian/rules debian/control
make -f ./debian/rules debian/copyright
gbp buildpackage --git-builder=true --git-cleaner=true --git-ignore-new
sbuild --no-clean-source --chroot-mode=unshare --nolog -s --force-orig-source -A -d unstable

dput -u digitalocean_emacs  ../emacs-snapshot*.changes(om[1,2])
