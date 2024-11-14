#!/bin/bash

cd data
curl --retry 10 --retry-all-errors --remote-name-all --parallel --parallel-max 150 "https://api.pwnedpasswords.com/range/{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}" > curl.log 2>&1

find . -type f -printf '%f\n' | egrep -ia '^[0-9a-f]{5}$' | xargs awk -F: '{ print FILENAME $1 ":" $2 }' | tr -d '\r' > hibp_all.txt

find . -type f -print | egrep -ia '/[0-9a-f]{5}$' | xargs rm
