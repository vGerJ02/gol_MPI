#!/bin/sh

git submodule update --init
echo "FindSDL2.cmake submodules cloned."
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=/usr/lib64/mpich/bin/mpicc
make install || { echo 'Installation failed.' ; exit 1; }
echo "Cleaning up build directory."
cd ..
rm -rf build
echo "Installation complete."
echo "Run ./conway from here or add to your PATH."
