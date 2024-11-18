# hibp: "Have I been pawned" database utilities

## Intro

High performance *downloader, query tool, server and utilities*

This very useful database is somewhat challenging to use locally
because of its sheer size. These utiliities make it easy and fast to
deal with the large data volume while being very effiicent on disk and
memory resouces.

Here is `hibp-download` running on a 400Mbit/s connection, averaging
~48MB/s which is very close to the theorectical maximum. At this
network speed, a download of the entire HIBP database, including
prefixing and joining the over 1 million files, converting to our
binary format and writing to disk, takes *under 12 minutes*.

![](https://github.com/oschonrock/hibp/blob/main/media/download.gif)

On a full 1Gbit/s connection this should take *under 5 minutes*.

### High peformance with a small memory and disk footprint

By deafult, this set of utilities uses a binary format to store and
search the data. The orginal text records are about 45 bytes per
password record, our binary format is 24 bytes, so storage
requirements are almost halved (21GB currently).

*If you don't like the binary format, you can always ouput the
conventional text version as well.*

Now that each record is a fixed width, and the records are maintained
in a sorted order, searches become very efficient, because we can use
random access binary search. There is an additional "table of
contents" feature to reduce disk access further at the expense of (by
default, but tunable) 2MB of memory. 

The local http server component is both multi threaded and event loop
driven for high efficiency. Even in a minimal configuration it should
be more than sufficient to back almost any site, at over 1,000req/s
on a single core.

The in memory footprint of these utilities is also very small, just a
few megabytes.

If you want to reduce diskspace even further, you could use utilities
like `hibp-topn` which will conveniently reduce a file to the `N` most
common pawned passwords. By default this is a ~1GB file for the 50,000,000
most common records.

### Quick start - Linux .deb systems

#### Install

[Download latest .deb and install](https://github.com/oschonrock/hibp/releases/latest)
```bash
wget -q https://github.com/oschonrock/hibp/releases/download/v0.1.0/hibp_0.1.1-1_amd64.deb
sudo dpkg -i hibp_0.1.1-1_amd64.deb

# and to remove again
sudo dpkg -r hibp
```

#### Usage

Download "Have I been pawned" database.
38GB download, uses 21GB of disk space and takes ~5/12 minutes on 1Gbit/400Mbit connection. Detailed progress is shown.

```bash
hibp.download hibp_all.bin
```

Serve the data on local http server.
```bash
hibp.server hibp_all.bin
```

Test the server (in a different terminal)

```bash
curl http://localhost:8082/check/plain/password123
```

The output will be the number of times that pasword has appeared in
leaks. Integrate this into your signup and login processes to show
warnings to the user that they using a compromised password.

For production, make this server a proper autostart "service" on your distribution. 

### Uninstall

To remove the package:
```bash
sudo dpkg -r hibp
```

## Building from source

### Installing build dependencies

refer to OS specific instructions

- [Linux](BUILD-linux.md)
- [FreeBSD](BUILD-FreeBSD.md)
- [Windows](BUILD-Windows.md)

### Compile for release
```bash
./build.sh -c gcc -b release
```

## Usage

### Run full download: `hibp-download`
Program will download the currently ~38GB of 1million 30-40kB text files from api.haveibeenpawned.com 
It does this using libcurl with curl_multi and 300 parallel requests on a single thread.
With a second thread doing the conversion to binary format and writing to disk.

*Warning* this will (currently) take just under 12mins on a 400Mbit/s connection and consume ~21GB of disk space
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
curl http://localhost:8082/check/plain/password

# output should be:
10434004

#if you pass --json to the server you will get
{count:10434004}

# if you feel more secure sha1 hashing the password in your client, you
# can also.

curl http://localhost:8082/check/sha1/5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8
10434004

```

For all options run `hibp-server --help`.

#### Basic performance evaluation using apache bench

```
# run server like this (--perf-test will uniquelt change the password for each request)

./build/gcc/release/hibp-server data/hibp_all.bin --perf-test

# and run apache bench like this (generate a somewhat random password to start):

hash=$(date | sha1sum); ab -c100 -n10000 "http://localhost:8082/check/plain/${hash:0:10}"

# These are the key figures from a short run on an old i5-3470 CPU @ 3.20GHz with 4 threads

Requests per second:    3166.96 [#/sec] (mean)
Time per request:       31.576 [ms] (mean)
Time per request:       0.316 [ms] (mean, across all concurrent requests)
```

This should be more than enough for almost any site, in fact you may
want to reduce the server to just one thread like so:

```
./build/gcc/release/hibp-server data/hibp_all.bin --perf-test --threads=1

hash=$(date | sha1sum); ab -c25 -n10000 "http://localhost:8082/check/plain/${hash:0:10}"

Requests per second:    1017.17 [#/sec] (mean)
Time per request:       24.578 [ms] (mean)
Time per request:       0.983 [ms] (mean, across all concurrent requests)
```

You can try the `--toc` feature on hibp-server which may improve
performance signficantly especially if you have limited free RAM for
the OS to cache the disk.


## Other utilities

`./build/gcc/release/hibp-topn`    : reduce a db to the N most common passwords (saves diskspace)

`./build/gcc/release/hibp-convert` : convert a text file into a binary file or vice-a-versa

`./build/gcc/release/hibp-sort`    : sort a binary file using external disk space (takes 3x space on disk)!

`./build/gcc/release/hibp-join`    : join the ~1M text files into one large binary one in arbitrary order (not useful since hibp-download)

In each case for all options run `program_name --help`.

## Under the hood

These utilities are written in C++ and centre around a `flat_file` class to model the db. 
- multi threaded concurrency and parallelism is used in the downloader,
  the server and the sorter.
- `libcurl`, `libevent` are used for the download
- `restinio` is used for the local server, based on `ASIO` for efficient concurrency
- libtbb is used for local sorting in `hibp-sort` and `hibp-topn`

## Future plans

- trying to get it packaged: snap, .deb, .rpm and FreeBSD port are priority, then
  windows installer. 

- Considering adding a php/pyhton/javascript extension so that queries
  can be trivially made from within those scripting environments
  without going through an http server
