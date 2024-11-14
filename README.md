# hibp

Have I been pwned database: High performance downloader, query tool, server and utilities

![](https://github.com/oschonrock/hibp/blob/main/media/download.gif)

This very useful database is somewhat challengint to use locally because the data size is so large. 
This set of utilities uses a binary format to store and search the data. The orginal text records are a 
40char SHA1 + ":" + an integer count + CR LF.

The binary format just stored the 20byte binary bytes of the SHA1 + 4 bytes integer
No delimiter is required because the record size is constant. Searches become easy, if the binary data is sorted, 
because we can use random access binary search.

Storage requirements are almost halved with the binary format (21GB currently). The in memory footprint of these 
utilities is very small and measured in a few megabytes.

These utilities are written in C++ and centre around a `flat_file` class to model the db. 
- libcurl, libevent are used for the download
- restinio is used for the local server
- libtbb is used for local sorting in parallel (mainly deprecated)

## Build environment and dependencies

refer to 

- [BUILD-linux.md](BUILD-linux.md)
- [BUILD-FreeBSD.md](BUILD-FreeBSD.md)
- [BUILD-Windows.md](BUILD-Windows.md)

## Compiling

### Compile in debug mode
```bash
# for compiling with gcc
./build.sh -c gcc -b debug

# Optional: for compiling with clang
./build.sh -c clang -b debug

```

### Run download in debug mode 
```bash
./build/gcc/debug/hibp-download --debug --limit=10 --parallel-max=3 hibp_sample.bin
```
You should see a bunch thread debug output, but no error and  `ls -lh hibp_sample.bin` should show ~5.3M

### compile for release
```bash

./build.sh -c gcc -b release
```

## Usage

### run full download: `hibp-download`
Program will download the currently ~38GB of 1million 30-40kB text files from api.haveibeenpawned.com 
It does this using libcurl with curl_multi and 300 parallel requests on a single thread.
With a second thread doing the conversion to binary format and writing to disk.

*Warning* this will (currently) take at about 13mins on a 1Gb connection and consume ~21GB of disk space
during this time:
- your network connection should be saturated with HTTP2 multiplexed requests
- `top` in threads mode (key `H`) should show 2 `hibp-download` threads.
- One "curl thread" with ~50-80% CPU and
- The "main thread" with ~15-30% CPU, primarily converting data to binary and writing to disk

```bash
time ./build/gcc/release/hibp-download hibp_all.bin

# you may see some warnings about failures and retries. If any transfers fails after 10 retries, programme will abort.
# after a permanent failure / abort, you can try rerunnung with `--resume` 
```

For all options run `hibp-download --help`.

### Run some sample "pawned password" queries from command line: `hibp-search`

This is a tiny program / debug utility that runs a single query against the downloaded binary database.

```bash
# replace 'password' as you wish

./build/gcc/release/hibp-search hibp_all.bin 'password'
# output should be 
search took                0.2699 ms
needle = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:-1
found  = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:10434004
```
Performance will be mainly down to your disk and be around 15-20ms per uncached query, and <0.2ms cached.

### Running a local server: `hibp-server`

You can run a high performance server for "pawned password queries" as follows. 
This is a simple "REST" server using the "restinio" library.
Searches does this by during "binary search" of the downloaded binary data database on disk. 
The process consumes <100Mb of virtual memory and < 5MB of resident memory. 
Note these are 100% accurate searches and not using some probabilistic "bloom filter" as in some similar projects.

```bash
./build/gcc/release/hibp-server hibp_all.bin
curl http://localhost:8082/password

# output should be:
10434004

#if you pass --json to the server you will get
{count:10434004}
```

For all options run `hibp-server --help`.

#### Basic performance evaluation using apache bench

```
# run server like this (--perf-test will uniquelt change the password for each request)

./build/gcc/release/hibp-server data/hibp_all.bin --perf-test

# and run apache bench like this (generate a somewhat random password to start):

hash=$(date | sha1sum); ab -c100 -n10000 "http://localhost:8082/${hash:0:10}"

# These the key figures from a short run on an old i5-3470 CPU @ 3.20GHz with 4 threads

Requests per second:    3166.96 [#/sec] (mean)
Time per request:       31.576 [ms] (mean)
Time per request:       0.316 [ms] (mean, across all concurrent requests)
```

This should be more than enough for almost any site, in fact you may want to reduce the server to just one thread like so:

```
./build/gcc/release/hibp-server data/hibp_all.bin --perf-test --threads=1

hash=$(date | sha1sum); ab -c25 -n10000 "http://localhost:8082/${hash:0:10}"

Requests per second:    1017.17 [#/sec] (mean)
Time per request:       24.578 [ms] (mean)
Time per request:       0.983 [ms] (mean, across all concurrent requests)
```

You can try the `--toc` feature on hibp-server which may improve
performance signficantly especially if you have limited free RAM for
to OS to cache the disk.


## Other utilities

`./fetch.sh` : curl command line to directly download the ~1M text files (approx 30-40kB each)
               also has find command line to join the above together (in arbitrary order!) and prefix the lines witin appropriately

`./build/gcc/release/hibp-convert` : convert a text file into a binary file or vice-a-versa

`./build/gcc/release/hibp-sort`    : sort a binary file using external disk space (takes 3x space on disk)!

`./build/gcc/release/hibp-join`    : join the ~1M text files into one large binary one in arbitrary order (not useful since hibp-download)

In each case for all options run `program_name --help`.

## Future uses

- Considering adding a php/pyhton/javascript extension so that queries
  can be trivially made from within those scripting environments
  without going through an http server
