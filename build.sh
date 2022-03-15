
#!/bin/bash

set -o errexit
set -o nounset
USAGE="Usage: $(basename $0) [-v | --verbose] [ gcc | clang ] [ test | reset | clean | debug | release | relwithdebinfo]"

CMAKE=cmake
BUILD=./build
TYPE=debug
COMPILER=clang
TEST=
CLEAN=
RESET=
VERBOSE=

for arg; do
  case "$arg" in
    --help|-h)    echo $USAGE; exit 0;;
    -v|--verbose) VERBOSE='VERBOSE=1'  ;;
    debug)        TYPE=debug ;;
    release)      TYPE=release ;;
    relwithdebinfo)      TYPE=relwithdebinfo ;;
    clang)        COMPILER=clang ;;
    gcc)          COMPILER=gcc ;;
    clean)        CLEAN=1 ;;
    test)         TEST="-DARRCMP_TEST=ON";;
    reset)        RESET=1 ;;
    *)            echo -e "unknown option $arg\n$USAGE" >&2;  exit 1 ;;
  esac
done

cd "$(realpath $(dirname $0))"

BUILD_DIR=$BUILD/$COMPILER/$TYPE

if [[ "$COMPILER" == "clang" ]]
then
    COMPILER_OPTIONS="-DCMAKE_C_COMPILER=clang-13 -DCMAKE_CXX_COMPILER=clang++-13"
else
    COMPILER_OPTIONS="-DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11"
fi

[[ -n $RESET && -d $BUILD_DIR ]] && rm -rf $BUILD_DIR
    
$CMAKE -S . -B $BUILD_DIR $COMPILER_OPTIONS -DCMAKE_BUILD_TYPE=$TYPE $TEST

[[ -n $CLEAN ]] && $CMAKE --build $BUILD_DIR --target clean

$CMAKE --build $BUILD_DIR -- -j8 $VERBOSE

cd $BUILD_DIR
