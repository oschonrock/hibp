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
CLEAN_FIRST=
GENERATEONLY=
VERBOSE=
TARGETS=

USAGE=$(cat <<-END

	Usage: $(basename $0) options

	Options:

	 -c | --compiler 	  specify the compile [ gcc | clang ] default is ${COMPILER}
	 -b | --buildtype 	  select buildtype [ debug | release | relwithdebinfo ]. default is ${BUILDTYPE}
	 -t | --targets 	  spefify targets  tgt1,tgt2,tgt3 
	 -g | --generate-only     only generate, don't build
	 --clean-first            cleans the selected targets before building (recompiles everything for those targets)
	 -p | --purge 		  completely wipe the selected build directory (deletes cmake config, implies --clean-first)
	 -v | --verbose 	  get verbose compiler command lines
	 -h | --help              show this info

END
)

options=$(getopt --options hvc:b:t:pg --long help,verbose,compiler:,buildtype:,targets:,purge,generate-only,clean-first -- "$@")

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
	    [[ ! $COMPILER =~ ^(gcc(-[0-9]+)?|clang(-[0-9]+)?)$ ]] && {
		echo "[--compiler | -c] must be gcc[-xx] or clang[-xx]"
		exit 1
            }
	    ;;
	-b|--buildtype)
	    shift
	    BUILDTYPE=${1,,}
	    [[ ! $BUILDTYPE =~ debug|release|relwithdebinfo ]] && {
		echo "[--buildtype | -b] must be debug|release|relwithdebinfo"
		exit 1
            }
	    ;;
	-t|--targets)
	    shift;
	    TARGETS="--target ${1//,/ }"; 
	    ;;
	-p|--purge)
            PURGE=1
	    ;;
	-g|--generateonly)
	    GENERATEONLY=1
	    ;;
	--clean-first)
	    CLEAN_FIRST="--clean-first"
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

if [[ "$COMPILER" =~ ^clang ]]
then
    C_COMPILER=$COMPILER
    CXX_COMPILER=${COMPILER/clang/clang++}
else
    C_COMPILER=$COMPILER
    CXX_COMPILER=${COMPILER/gcc/g++}
fi

if command -v ccache > /dev/null 2>&1
then
    CACHE="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
else
    echo -e '\033[0;31m'"Consider installing \`ccache\` for extra speed!"'\033[0m' 1>&2
    CACHE=""
fi

COMPILER_OPTIONS="-DCMAKE_C_COMPILER=$C_COMPILER -DCMAKE_CXX_COMPILER=$CXX_COMPILER -DCMAKE_BUILD_TYPE=$BUILDTYPE"

[[ -n $PURGE && -d $BUILD_DIR ]] && rm -rf $BUILD_DIR

$CMAKE -GNinja -S . -B $BUILD_DIR $CACHE -DCMAKE_COLOR_DIAGNOSTICS=ON $COMPILER_OPTIONS
GEN_RET=$?

[[ $GEN_RET ]] && rm -f ./compile_commands.json && ln -s $BUILD_DIR/compile_commands.json .

[[ -n $GENERATEONLY ]] && exit $GEN_RET

$CMAKE --build $BUILD_DIR $CLEAN_FIRST $TARGETS -- $VERBOSE
