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
binary format and writing to disk, takes *under 12 minutes* (the
official downloader takes over an hour on the same connection).

![](https://github.com/oschonrock/hibp/blob/main/media/download.gif)

On a full 1Gbit/s connection this take *around 6 minutes*, as shown here under Windows:

![](https://github.com/oschonrock/hibp/blob/main/media/windows_download.png)

## Quick start - Linux amd64 .deb systems (for others see below!)

#### Install

[Download latest .deb and install](https://github.com/oschonrock/hibp/releases/latest)
```bash
wget -q https://github.com/oschonrock/hibp/releases/download/v0.4.1/hibp_0.4.1-1_amd64.deb
sudo apt install ./hibp_0.4.1-1_amd64.deb  # will install minimal dependencies (eg `libevent`)
```

#### Usage

Download "Have I been pawned" database.  38GB download, uses 21GB of
disk space and takes ~6/12 minutes on 1Gbit/400Mbit
connection. Detailed progress is shown.

```bash
hibp-download hibp_all.sha1.bin
```

Serve the data on a local http server.

```bash
hibp-server --sha1-db=hibp_all.sha1.bin
```

Try out the server (in a different terminal)

```bash
curl http://localhost:8082/check/plain/password123

# or if you prefer

curl http://localhost:8082/check/sha1/CBFDAC6008F9CAB4083784CBD1874F76618D2A97
```

The output will be the number of times that pasword has appeared in
leaks. Integrate this into your signup and login processes to show
warnings to the user that they using a compromised password.

For production, make this server a proper "autostart service" on your distribution. 

#### Uninstall

To remove the package:
```bash
sudo apt remove hibp
```

## Design: High performance with a small memory, disk and CPU footprint

These utilities are designed to have a very modest resource
footprint. By default, they use a binary format to store and search
the data. The orginal text records are about 45 bytes per password
record, our binary format is 24 bytes, so storage requirements are
almost halved (21GB currently).

*If you don't like the binary format, you can always ouput the
conventional text version as well.*

In the binary format each record is a fixed width, and the records are
maintained in a sorted order, so searches become very efficient,
because we can use random access binary search. There is an additional
"table of contents" feature (see `--toc`below) to reduce disk access
further at the expense of only 4MB of memory.

The local http server component is both multi threaded and event loop
driven for high efficiency. Even in a minimal configuration it should
be more than sufficient to back almost any site, at over 1,000req/s
on a single core.

The in memory footprint of these utilities is also very small, just a
few megabytes.

If you want to reduce diskspace even further, you could use utilities
like `hibp-topn` (see below) which will conveniently reduce a file to
the `N` most common pawned passwords. By default this is a ~1GB file
for the 50,000,000 most commonly leaked passwords.

## Building from source

`hibp` has very modest dependencies and should compile without
problems on many platforms. gcc >= 10 and clang >= 11 are tested
on several `.deb` and `.rpm` based systems, under FreeBSD and under
Windows (MSYS2/mingw).

You will likely also suceed with minimal platforms like the
raspberry-pi.

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

Program will download the currently ~38GB of data, containing 1
million 30-40kB text files from api.haveibeenpawned.com It does this
using `libcurl` with `curl_multi` and 300 parallel requests
(adjustable) on a single thread.  A second thread does the
conversion to binary format and writing to disk.

*Warning* this will (currently) take just around 6mins on a 1Gb/s
connection and consume ~21GB of disk space during this time:
- your network connection will be saturated with HTTP2 multiplexed requests
- `top` in threads mode (key `H`) should show 2 `hibp-download` threads.
- One "curl thread" with ~50-80% CPU and
- The "main thread" with ~15-30% CPU, primarily converting data to
  binary and writing to disk

```bash
./build/gcc/release/hibp-download hibp_all.sha1.bin
```

If any transfer fails, even after 5 retries, the programme will
abort. In this case, you can try rerunning with `--resume`.

For all options run `hibp-download --help`.

### Run some sample "pawned password" queries from the command line: `hibp-search`

This is a tiny program / debug utility that runs a single query
against the downloaded binary database.

```bash
# replace 'password' as you wish

./build/gcc/release/hibp-search hibp_all.sha1.bin password
# output should be 
search took                0.2699 ms
needle = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:-1
found  = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:10434004
```

Performance will be mainly down to your disk and be 5-8ms per uncached
query, and <0.3ms cached.  See below for further improved performance
with `--toc`

### NT Hash (AKA NTLM)

The compromised password database is also available using the NTLM
hash, rather than sha1. This may be useful if auditing local
Windows server authentication systems.

```bash
./build/gcc/release/hibp-download --ntlm hibp_all.ntlm.bin
```
and then search
```bash
./build/gcc/release/hibp-search --ntlm hibp_all.ntlm.bin password
```
or maybe "by hash" rather than plaintext password? 
```bash
./build/gcc/release/hibp-search --ntlm hibp_all.ntlm.bin --hash 00000011059407D743D40689940F858C
```

### Running a local server: `hibp-server`

You can run a high performance server for "pawned password queries" as
follows.  This is a simple "REST" server using the "restinio" library.
The server process consumes <5MB of resident memory.

```bash
./build/gcc/release/hibp-server hibp_all.sha1.bin
curl http://localhost:8082/check/plain/password

# output should be:
10434004

#if you pass --json to the server you will get
{count:10434004}

# if you feel more secure sha1 hashing the password in your client, you
# can also do this

curl http://localhost:8082/check/sha1/5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8
10434004

```

For all options run `hibp-server --help`.

#### Basic performance evaluation using apache bench

Run server like this (--perf-test will uniquefy change the password for each request)
```
./build/gcc/release/hibp-server data/hibp_all.bin --perf-test
```

And run apache bench like this (generating a somewhat random password to start with):

```
hash=$(date | sha1sum); ab -c100 -n10000 "http://localhost:8082/check/plain/${hash:0:10}"
```

These are the key figures from a short run on an old i5-3470 CPU @ 3.20GHz with 3 threads
(one thread is consumed running `ab`).

```
Requests per second:    3166.96 [#/sec] (mean)
Time per request:       31.576 [ms] (mean)
Time per request:       0.316 [ms] (mean, across all concurrent requests)
```

This should be more than enough for almost any site, in fact you may
want to reduce the server to just one thread like so:

```
./build/gcc/release/hibp-server data/hibp_all.bin --perf-test --threads=1
```

```
hash=$(date | sha1sum); ab -c25 -n10000 "http://localhost:8082/check/plain/${hash:0:10}"

Requests per second:    1017.17 [#/sec] (mean)
Time per request:       24.578 [ms] (mean)
Time per request:       0.983 [ms] (mean, across all concurrent requests)
```

#### Enhanced performance for constrained devices: `--toc`

If you are runnning this database on a constrained device, with
limited free RAM or a slow disk, You may want to try using the "table
of contents" features, which builds an index into the "chapters" of
the database and then holds this index in memory.

This only consumes an additional 4MB of RAM by default, but maintains
excellent performance even without any OS level disk caching.

`--toc` is available on the `hibp-search` test utility, and the `hibp-server`. 

The first run with `--toc` builds the index, which takes about 1
minute, depending on your sequential disk speed. `hibp-search` shows
that completely uncached queries *reduce from 5-8ms to just 0.7ms*.

#### The "ultimate" server

Maybe you want to serve plaintext, sha1 and ntlm at the same time,
while taking advantage extra of `--toc` performance. Here is the full
set of commands script for that, assuming the programs are on your
`PATH` for brevity:

```bash
hibp-download --sha1 hibp_all.sha1.bin
hibp-download --ntlm hibp_all.ntlm.bin

hibp-server --sha1-db=hibp_all.sha1.bin --ntlm-db=hibp_all.ntlm.bin --toc
```

Output:
```
Make a request to any of:
http://localhost:8082/check/plain/password123  [using sha1 db]
http://localhost:8082/check/sha1/CBFDAC6008F9CAB4083784CBD1874F76618D2A97
http://localhost:8082/check/ntlm/A9FDFA038C4B75EBC76DC855DD74F0DA
```

And if you wanted to conserve diskspace you could, use `hibp-topn`:

```bash
hibp-topn hibp_all.sha1.bin -o hibp_topn.sha1.bin
hibp-topn --ntlm hibp_all.ntlm.bin -o hibp_topn.ntlm.bin

hibp-server --sha1-db=hibp_topn.sha1.bin --ntlm-db=hibp_topn.ntlm.bin --toc
```

You can now remove the really big files, if the top 50million entries
is enough for you.

## Other utilities

`hibp-topn`    : reduce a db to the N most common passwords (saves diskspace)

`hibp-convert` : convert a text file into a binary file or vice-a-versa

`hibp-sort`    : sort a binary file using external disk space (Warning: takes 3x space on disk)

In each case, for all options run `program-name --help`.

## What is `./build.sh`?

It's just a convenience wrapper around `cmake`, mainly to select
`cmake` `-D` options with less typing. See `./build.sh --help` for options.

You can use `./build.sh --verbose` to see how `./build.sh` is invoking
`cmake` (as well as making `cmake` verbose).

## Running tests

There is a significant set of unit, integration and system tests -
although not 100% coverage at this point.

You can run them with one of these options:
- from the `./build.sh` convenience script with `--run-tests`
- by using `ccmake` to set `HIBP_TEST=ON` 
- by passing `-DHIBP_TEST=ON` to cmake directly

## Why are you using http (no TLS)?

The main intention is for this be a local server, binding to
`localhost` only, and thats the default behaviour. There is no request
logging, so `http` is a secure and simple architecture. 

Of course, if you want to serve beyond localhost, you **should
definitely** either *use a reverse proxy* in front of hibp-server, or
modify `app/hibp-server.cpp` and *recompile with TLS support*.

## Under the hood

These utilities are written in C++ and centre around a `flat_file` class to model the db. 
- multi threaded concurrency and parallelism is used in
	`hibp-download`, `hibp-server`, `hibp-sort` and `hibp-topn`.
- `libcurl`, `libevent` are used for the highly concurrent download
- `restinio` is used for the local server, based on `ASIO` for efficient concurrency
- [`arrcmp`](https://github.com/oschonrock/arrcmp) is used as a high
  performance, compile time optimised replacement for `memcmp`, which
  makes agressive use of your CPU's vector instructions
- libtbb is used for local sorting in `hibp-sort` and
	`hibp-topn`. Note that for the parallelism (ie PSTL using libtbb)
	you currently have to compile from source, but this only has a
	small effect on `hibp-sort` and `hibp-topn`. And due to
	portability annoyances and a bug in libstd++, this is disabled by
	default, and you need to turn `HIBP_WITH_PSTL=ON` to use it.

## Future plans

- More packaging: 
  - Get the .deb accepted into Debian 
  - publish a .rpm 
  - get it into a FreeBSD port
  - produce a windows installer 

- Consider adding a php/pyhton/javascript extension so that queries
  can be trivially made from within those scripting environments
	  without going through an http server
