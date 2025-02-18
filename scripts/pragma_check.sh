#!/usr/bin/env bash

find src include \( -name '*.hpp' -o -name '*.h' \) -print0 | while read -d $'\0' header
do
    if ! egrep '^#pragma once' $header >/dev/null
    then
        echo "missing #pragma once: $header"
    fi
done
