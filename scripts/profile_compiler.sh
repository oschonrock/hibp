#! /usr/bin/env bash

if ! egrep '^[^#]+-ftime-trace' CMakeLists.txt > /dev/null 
then
    echo "ensure you have uncommented  -ftime-trace in CMakeLists.txt" 1>&2
    exit 1
fi
if git status app | egrep modified
then
    echo "ensure you have comttted, stashed or reverted any changes in app/" 1>&2 
    exit 1
fi

rm -rf build/clang/debug

for SRC_FILE in app/*.cpp; do
    TARGET=$(basename -s .cpp "$SRC_FILE")
    echo  1>&2
    echo "profiling target $TARGET" 1>&2
    echo "===================================="  1>&2
    
    time ./build.sh -c clang -b debug -t "$TARGET" | egrep -v '^--'
    echo '//' >> "$SRC_FILE"  # make small modification
    time ./build.sh -c clang -b debug -t "$TARGET" | egrep -v '^--'
    git checkout "$SRC_FILE" > /dev/null 2>&1 # revert change
    echo "open chrome at chrome://tracing  and load  build/clang/debug/CMakeFiles/$TARGET.dir/app/$SRC_FILE.json"
done
