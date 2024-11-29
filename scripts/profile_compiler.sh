#! /usr/bin/env bash

if ! egrep '^[^#]+-ftime-trace' CMakeLists.txt > /dev/null 
then
   echo "ensure you have uncommented  -ftime-trace in CMakeLists.txt" 1>&2 
fi
if git status app | egrep modified
then
   echo "ensure you have comttted, stashed or reverted any changes in app/" 1>&2 
fi

rm -rf build

for SRC_FILE in $(ls app/*.cpp); do
    TARGET=$(basename -s .cpp "$SRC_FILE")
    echo 
    echo "profilig target $TARGET" 1>&2
    echo
    
    time ./build.sh -c clang -t $TARGET | egrep -v '^--'
    echo '//' >> "$SRC_FILE"  # make small modification
    time ./build.sh -c clang -t $TARGET | egrep -v '^--'
    git checkout "$SRC_FILE" > /dev/null # revert change
done
