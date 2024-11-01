#!/bin/bash

set -o errexit
set -o nounset
USAGE="Usage: $(basename $0) [-v | --verbose] [ gcc | clang ] [ test | bench | reset | clean | debug | release | relwithdebinfo]"

CMAKE=cmake
BUILD=./build
TYPE=debug
COMPILER=clang
TEST=
BENCH=
CLEAN=
RESET=
VERBOSE=

for arg; do
  case "$arg" in
    --help|-h)    echo $USAGE; exit 0;;
    -v|--verbose) VERBOSE='--verbose'  ;;
    debug)        TYPE=debug ;;
    release)      TYPE=release ;;
    relwithdebinfo)      TYPE=relwithdebinfo ;;
    clang)        COMPILER=clang ;;
    gcc)          COMPILER=gcc ;;
    clean)        CLEAN=1 ;;
    test)         TEST="-DHIBP_TEST=ON";;
    bench)        BENCH="-DHIBP_BENCH=ON";;
    reset)        RESET=1 ;;
    *)            echo -e "unknown option $arg\n$USAGE" >&2;  exit 1 ;;
  esac
done

cd "$(realpath $(dirname $0))"

BUILD_DIR=$BUILD/$COMPILER/$TYPE

if [[ "$COMPILER" == "clang" ]]
then
    C_COMPILER=clang 
    CXX_COMPILER=clang++
else
    C_COMPILER=gcc 
    CXX_COMPILER=g++
fi

if command -v ccache &> /dev/null
then
    CACHE="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
else
    echo -e '\033[0;31m'"Consider installing \`ccache\` for extra speed!"'\033[0m' 1>&2
    CACHE=""
fi

COMPILER_OPTIONS="-DCMAKE_C_COMPILER=$C_COMPILER -DCMAKE_CXX_COMPILER=$CXX_COMPILER "

[[ -n $RESET && -d $BUILD_DIR ]] && rm -rf $BUILD_DIR
    
$CMAKE -GNinja -S . -B $BUILD_DIR $CACHE -DCMAKE_COLOR_DIAGNOSTICS=ON $COMPILER_OPTIONS -DCMAKE_BUILD_TYPE=$TYPE $TEST $BENCH

[[ -n $CLEAN ]] && $CMAKE --build $BUILD_DIR --target clean

$CMAKE --build $BUILD_DIR -- $VERBOSE

cd $BUILD_DIR
