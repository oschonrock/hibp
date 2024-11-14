#!/usr/bin/env bash

git submodule update --init --recursive
cd ext/restinio
sudo gem install Mxx_ru   # install ruby gem required for restinio dependency installation
mxxruexternals            # install those deps
cd ../..
snapcraftctl build
