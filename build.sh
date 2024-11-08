#!/usr/bin/env bash

set -o errexit
set -o nounset

CMAKE=cmake
BUILDROOT=./build
BUILDTYPE=debug
COMPILER=gcc
TEST=
BENCH=
PURGE=
GENERATEONLY=
VERBOSE=
TARGETS=

USAGE=$(cat <<-END
	Usage: $(basename $0) options

	Options:

	[ -c | --compiler ]	  specify the compile [ gcc | clang ] default is ${COMPILER}
	[ -b | --buildtype ]	  select buildtype [ debug | release | relwithdebinfo ]. default is ${BUILDTYPE}
	[ -t | --targets ]	  spefify targets  tgt1,tgt2,tgt3 
	[ -p | --purge ]	  complete wipe the selected build directory
	[ -g | --generate-only ]  only generate, don't build
	[ -v | --verbose]	  get verbose compiler command lines

END
)


options=$(getopt --options hvc:b:t:pg --long help,verbose,compiler:,buildtype:,targets:,purge,generate-only -- "$@")
[ $? -eq 0 ] || { 
    echo "Incorrect options provided"
    echo "$USAGE"
    exit 1
}
eval set -- "$options"
while true; do
    case "$1" in
	--help|-h)
	    echo "$USAGE";
	    exit 0;;
	-v|--verbose)
	    VERBOSE='--verbose'
	    ;;
	-c|--compiler)
	    shift
	    COMPILER=$1
	    [[ ! $COMPILER =~ gcc|clang ]] && {
		echo "[--compiler | -c] must be gcc or clang"
		exit 1
            }
	    ;;
	-b|--buildtype)
	    shift
	    BUILDTYPE=${1,,} # lowercase, requires bash 4.0
	    [[ ! $BUILDTYPE =~ debug|release|relwithdebinfo ]] && {
		echo "[--buildtype | -b] must be debug|release|relwithdebinfo"
		exit 1
            }
	    ;;
	-t|--targets)
	    shift;
	    TARGETS="--target ${1/,/ }"; 
	    ;;
	-p|--purge)
            PURGE=1
	    ;;
	-g|--generateonly)
	    GENERATEONLY=1
	    ;;
	--)
	    shift
	    break
	    ;;
    esac
    shift
done

cd "$(realpath $(dirname $0))"

BUILD_DIR=$BUILDROOT/$COMPILER/$BUILDTYPE

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

COMPILER_OPTIONS="-DCMAKE_C_COMPILER=$C_COMPILER -DCMAKE_CXX_COMPILER=$CXX_COMPILER -DCMAKE_BUILD_TYPE=$BUILDTYPE"

[[ -n $PURGE && -d $BUILD_DIR ]] && rm -rf $BUILD_DIR

$CMAKE -GNinja -S . -B $BUILD_DIR $CACHE -DCMAKE_COLOR_DIAGNOSTICS=ON $COMPILER_OPTIONS

[[ -n $GENERATEONLY ]] && exit $?

$CMAKE --build $BUILD_DIR $TARGETS -- $VERBOSE

