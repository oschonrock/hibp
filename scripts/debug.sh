#!/usr/bin/env bash


./build.sh -c gcc -b debug -t hibp_search
./build/gcc/debug/hibp-download hibp_sample.sha1.bin --force --no-progress 2>/dev/null 1>/dev/null &
pid=$!

sudo gdb --ex 'break queuemgt.cpp:167' -ex continue -ex 'info threads' ./build/gcc/debug/hibp-download $pid

echo "killing $pid"
kill $pid
