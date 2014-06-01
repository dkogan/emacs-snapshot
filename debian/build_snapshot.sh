#!/bin/bash
set -e -x

cd `dirname $0`/..

git fetch upstream
git reset --hard
git clean -ffdx

git checkout upstream
git merge -m 'Merged upstream' upstream/master

git checkout master
git merge -m 'merging new upstream' upstream

gbp-pq rebase
gbp-pq export
git add debian/patches/

# I let this fail because no patch updates may be necessary
git commit -m 'patch update' debian/patches/ || true

# need to make this non-interactive
dch -r -v `date +'2:%Y%m%d-1'`
git commit -m 'new snapshot' debian/changelog

git clean -ffdx; git reset --hard
./debian/rules debian/control
./debian/rules debian/copyright
git-buildpackage --git-ignore-new


# dch should be this non-interactive and not UNRELEASED
# GFDL issues
# debsign issues
