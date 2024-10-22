#!/bin/bash

cd data
curl --retry 10 --retry-all-errors --remote-name-all --parallel --parallel-max 150 "https://api.pwnedpasswords.com/range/{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}{0,1,2,3,4,5,6,7,8,9,A,B,C,D,E,F}" > curl.log 2>&1

find . -type f -print | egrep -ia '/[0-9a-f]{5}$' | xargs -r -d '\n' awk -F: '{ sub(/\r$/,""); print substr(FILENAME, length(FILENAME)-4, 5) $1 ":" $2 }' > hibp_all.txt

find . -type f -print | egrep -ia '/[0-9a-f]{5}$' | xargs -r -d '\n' rm

