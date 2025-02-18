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
NOPCH="-DNOPCH=OFF"      # override cmake cache
TESTS=
GENERATEONLY=
VERBOSE=
TARGETS=()
INSTALL=
INSTALL_PREFIX=

USAGE=$(cat <<-END

    Convenience script around $CMAKE invocation

    Usage: $(basename "$0") options

    Options:

     -c | --compiler          specify the compiler [ gcc | clang ] default is ${COMPILER}
     -b | --buildtype         select buildtype [ debug | release | relwithdebinfo ]. default is ${BUILDTYPE}
     -t | --targets           specify targets tgt1,tgt2,tgt3
     -g | --generate-only     only generate, don't build
     --clean-first            cleans the selected targets before building (recompiles everything for those targets)
     -p | --purge             completely wipe the selected build directory (deletes cmake config, implies --clean-first)
     --install                install after building
     --install-prefix         the prefix for install, defaults to /usr/local
     --run-tests              run all tests immediately after build (is "sticky" in cmake cache)
     --skip-tests             skip running tests (is "sticky" in cmake cache)
     -v | --verbose           get verbose compiler command lines
     -h | --help              show this info

END
)

GETOPT=getopt
if command -v /usr/local/bin/getopt > /dev/null 2>&1; then
    GETOPT='/usr/local/bin/getopt'
fi

options=$("$GETOPT" --options hvc:b:t:pg --long help,verbose,compiler:,buildtype:,targets:,purge,generate-only,run-tests,skip-tests,install,install-prefix:,clean-first,nopch -- "$@")

eval set -- "$options"
while true; do
    case "$1" in
    --help|-h)
        echo "$USAGE"
        exit 0
        ;;
    -v|--verbose)
        VERBOSE='--verbose'
        ;;
    -c|--compiler)
        shift
        COMPILER=$1
        [[ ! "$COMPILER" =~ ^(gcc(-[0-9]+)?|clang(-[0-9]+)?)$ ]] && {
            echo "[--compiler | -c] must be gcc[-xx] or clang[-xx]" >&2
            exit 1
        }
        ;;
    -b|--buildtype)
        shift
        BUILDTYPE=${1,,}
        [[ ! "$BUILDTYPE" =~ debug|release|relwithdebinfo ]] && {
            echo "[--buildtype | -b] must be debug|release|relwithdebinfo" >&2
            exit 1
        }
        ;;
    -t|--targets)
        shift
        [ -z "$1" ] && echo "--targets parameter must not be empty" >&2 && exit 1
        set -- ${1//,/ }
        TARGETS=(
            "--target"
            "$@"
        )
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
    --install)
        INSTALL=1
        ;;
    --install-prefix)
        shift
        INSTALL_PREFIX="-DCMAKE_INSTALL_PREFIX=$1"
        ;;
    --nopch)
        NOPCH="-DNOPCH=ON"
        ;;
    --run-tests)
        TESTS="-DHIBP_TEST=ON"
        ;;
    --skip-tests)
        TESTS="-DHIBP_TEST=OFF"
        ;;
    --)
        shift
        break
        ;;
    esac
    shift
done

[[ $# -gt 0 ]] && echo "unexpected argument '$1'" && exit 1

cd "$(realpath "$(dirname "$0")")"

BUILD_DIR=$BUILDROOT/$COMPILER/$BUILDTYPE

if [[ "$COMPILER" =~ ^clang ]]; then
    C_COMPILER=$COMPILER
    CXX_COMPILER=${COMPILER/clang/clang++}
else
    C_COMPILER=$COMPILER
    CXX_COMPILER=${COMPILER/gcc/g++}
fi

BUILD_OPTIONS=(
    "-DCMAKE_C_COMPILER=$C_COMPILER"
    "-DCMAKE_CXX_COMPILER=$CXX_COMPILER"
    "-DCMAKE_BUILD_TYPE=$BUILDTYPE"
)

[ -n "$TESTS" ] && BUILD_OPTIONS+=("$TESTS")
[ -n "$INSTALL_PREFIX" ] && BUILD_OPTIONS+=("$INSTALL_PREFIX")

if command -v ccache > /dev/null 2>&1; then
    BUILD_OPTIONS+=(
        "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
        "-DCMAKE_C_COMPILER_LAUNCHER=ccache"
    )
fi

if command -v mold > /dev/null 2>&1; then
    BUILD_OPTIONS+=(
        "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold"
        "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=mold"
    )
fi

[[ -n "$PURGE" && -d "$BUILD_DIR" ]] && rm -rf "$BUILD_DIR"

GEN_CMD=(
    "$CMAKE"
    "-GNinja"
    "-S" "."
    "-B" "$BUILD_DIR"
    "-DCMAKE_COLOR_DIAGNOSTICS=ON"
    "-DBINFUSE_TEST=OFF"
    "${BUILD_OPTIONS[@]}"
    "$NOPCH"
)
[[ -n "$VERBOSE" ]] && echo "${GEN_CMD[@]}"
"${GEN_CMD[@]}" && GEN_RET=$? || GEN_RET=$?

[[ $GEN_RET -eq 0 ]] && rm -f ./compile_commands.json && ln -s "$BUILD_DIR/compile_commands.json" .

[[ -n "$GENERATEONLY" ]] && exit $GEN_RET

BUILD_CMD=(
    "$CMAKE"
    "--build" "$BUILD_DIR"
)
[ -n "${TARGETS[@]}" ] && BUILD_CMD+=("${TARGETS[@]}")
[ -n "$CLEAN_FIRST" ] && BUILD_CMD+=("$CLEAN_FIRST")
BUILD_CMD+=(
    "--"
    "$VERBOSE"
)

[[ -n $VERBOSE ]] && echo "${BUILD_CMD[@]}"
"${BUILD_CMD[@]}" && BUILD_RET=$? || BUILD_RET=$?

if [[ $BUILD_RET -eq 0 && -n "$INSTALL" ]]; then
    cmake --install "$BUILD_DIR"
else
    exit $BUILD_RET
fi
