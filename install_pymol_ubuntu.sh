#!/bin/sh
sudo apt-get build-dep pymol
sudo apt-get install python-tk python-pmw

if [ -z "$PREFIX" ]; then
    PREFIX=~/pymol
fi
MODULES=$PREFIX/modules

export CPPFLAGS="-std=c++0x"

rm -rf build
python setup.py --jobs 10 build install \
    --home=$PREFIX \
    --install-lib=$MODULES \
    --install-scripts=$PREFIX
