
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
    -v|--verbose) VERBOSE='VERBOSE=1'  ;;
    debug)        TYPE=debug ;;
    release)      TYPE=release ;;
    relwithdebinfo)      TYPE=relwithdebinfo ;;
    clang)        COMPILER=clang ;;
    gcc)          COMPILER=gcc ;;
    clean)        CLEAN=1 ;;
    bench)        BENCH="-DHIBP_BENCH=ON";;
    reset)        RESET=1 ;;
    *)            echo -e "unknown option $arg\n$USAGE" >&2;  exit 1 ;;
  esac
done

cd "$(realpath $(dirname $0))"

BUILD_DIR=$BUILD/$COMPILER/$TYPE

if [[ "$COMPILER" == "clang" ]]
then
    # detect version specific preferred name
    if command -v clang-13 &> /dev/null
    then
      C_COMPILER=clang-13 
      CXX_COMPILER=clang++-13
    else
      C_COMPILER=clang 
      CXX_COMPILER=clang++
    fi
else
    # detect version specific preferred name
    if command -v gcc-11 &> /dev/null
    then
      C_COMPILER=gcc-11
      CXX_COMPILER=g++-11
    else
      C_COMPILER=gcc 
      CXX_COMPILER=g++
    fi
fi
COMPILER_OPTIONS="-DCMAKE_C_COMPILER=$C_COMPILER -DCMAKE_CXX_COMPILER=$CXX_COMPILER"

[[ -n $RESET && -d $BUILD_DIR ]] && rm -rf $BUILD_DIR
    
$CMAKE -S . -B $BUILD_DIR $COMPILER_OPTIONS -DCMAKE_BUILD_TYPE=$TYPE $TEST $BENCH

[[ -n $CLEAN ]] && $CMAKE --build $BUILD_DIR --target clean

$CMAKE --build $BUILD_DIR -- -j8 $VERBOSE

cd $BUILD_DIR
