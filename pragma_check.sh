#!/usr/bin/env bash

for header in $(find src include -name '*.hpp' -or -name '*.h')
do
    if ! egrep '^#pragma once' $header >/dev/null
    then
	echo "missing #pragma once: $header"
    fi
done
