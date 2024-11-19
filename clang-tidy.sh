#!/usr/bin/env bash

find app/ src/ -type f -name '*.cpp' | parallel -q clang-tidy --quiet --config-file .clang-tidy -header-filter='.*/hibp/include/.*'
