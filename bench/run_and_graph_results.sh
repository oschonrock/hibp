#!/usr/bin/bash

OPTIND=1
rerun=0

base="$(dirname $(realpath $0))"

while getopts ":r" opt
do
    case "$opt" in
	h|\?)
	    echo " -r to rerun benchmarks "
	    exit 0
	    ;;
	r)  rerun=1
	    ;;
    esac
done

shift $((OPTIND-1))

csvfile="$base/results/cmp.csv"

if [[ $rerun == 1 ]]
then
    "$base/../build/clang/release/bench/arrcmp" --benchmark_format=csv > "$csvfile"
fi

python "$base/../ext/google_benchmark_plot/plot.py" < "$csvfile" &
