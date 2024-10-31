# hibp
Have I been pwned database: High performance downloader, query tool, server and utilities

This very useful database is somewhat challengint to use locally because the data size is so large. 
This set of utilities uses a binary format to store and search the data. The orginal text records are a 
40char SHA1 + ":" + an integer count + <CR><LF>

The binary format just stored the 20byte binary bytes of the SHA1 + 4 bytes integer
No delimiter is required because the record size is constant. Searches become easy, if the binary data is sorted, 
because we can use random access binary search.

Storage requirements are almost halved with the binary format (21GB currently). The in memory footprint of these 
utilities is very small and measured in a few megabytes.

These utilities are written in C++ and centre around a `flat_file` class to model the db. 
- libcurl, libevent are used for the download
- restinio is used for the local server
- libtbb is used for local sorting in parallel (mainly deprecated)

## Building

### Ubuntu 24.04

#### Install Dependencies
```bash
sudo apt install build-essential git cmake libcurl4-openssl-dev libevent-dev ruby libtbb-dev
git clone https://github.com/oschonrock/hibp.git
cd hibp
git submodule update --init --recursive
cd ext/restinio
sudo gem install Mxx_ru   # install ruby gem required for restinio dependency installation
mxxruexternals            # install those deps
cd ../..

# optional: for compilng with clang also:
sudo apt install clang gcc-14 g++-14  # need gcc-14 because clang tries to use its stdlibc++ version

```

#### Fix bug in libstdc++ parallel algos integration with libtbb-dev
see here
https://gcc.gnu.org/bugzilla/show_bug.cgi?id=117276

install the patch provided in bug report above into both versions of libstdc++ now installed on the machine

```bash
cd /usr/include/c++/13/
wget -qO - https://gcc.gnu.org/bugzilla/attachment.cgi?id=59419 | sudo patch --backup --strip 5
cd ../14/
wget -qO - https://gcc.gnu.org/bugzilla/attachment.cgi?id=59419 | sudo patch --backup --strip 5
cd ~/hibp
```

#### Compile in debug mode
```bash
# for compiling with gcc
./build.sh gcc debug

# Optional: for compiling with clang
./build.sh clang debug

```

#### Run download in debug mode 
```bash
./build/gcc/debug/hibp_download > hibp_sample.bin
```
You should see a bunch thread debug output, but no error and  `ls -lh hibp_sample.bin` should show ~5.3M

#### compile for release and run download: `hibp_download`
Program will download the currently ~38GB of 1million 30-40kB text files from api.haveibeenpawned.com 
It does this using libcurl with curl_multi and 300 parallel requests on a single thread.
With a second thread doing the conversion to binary format and writing to disk.

```bash

./build.sh gcc release

# warning this will (currently) take at about 13mins on a 1Gb connection and consume ~21GB of disk space
# during this time your network connection should be saturated with HTTP2 multiplexed requests
# `top` in threads mode (key `H`) should show 2 `hibp_download` threads.
# One "curl thread" with ~50-80% CPU and
# the "main thread" with ~15-30% CPU, primarily converting data to binary and writing to disk

time ./build/gcc/release/hibp_download > hibp_all.bin

# you may see some warnings about failures and retries. If any transfers fails after 10 retries, programme will abort.
```

### Run some sample "pawned password" queries from command line: `hibp_search`

This is a tiny program / debug utility that runs a single query against the downloaded binary database.

```bash
# replace 'password' as you wish

./build/gcc/release/hibp_search hibp_all.bin 'password'
# output should be 
search took                0.2699 ms
needle = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:-1
found  = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:10434004
```
Performance will be mainly down to your disk and be around 15-20ms per uncached query, and <0.2ms cached.

### Running a local server: `hibp_server`

You can run a high performance server for "pawned password queries" as follows. 
This is a simple "REST" server using the "restinio" library.
Searches does this by during "binary search" of the downloaded binary data database on disk. 
The process consumes <100Mb of virtual memory and < 5MB of resident memory. 
Note these are 100% accurate searches and not using some probabilistic "bloom filter" as in some similar projects.

```bash
./build/gcc/release/hibp_server hibp_all.bin
curl http://localhost:8082/password

# output should be:
count=10434004
```
Performance should be > 100 requests/second for a single core with zero latency and max concurrency but is highly disk dependent. 
Performance feedback on different system is very welcome. 

### Other utilities

`./fetch.sh` : curl command line to directly download the ~1M text files (approx 30-40kB each)
               also has find command line to join the above together (in arbitrary order!) and prefix the lines witin appropriately

`./build/gcc/release/hibp_join`    : join the ~1M text files into one large binary one in arbitrary order (not useful since hibp_download)

`./build/gcc/release/hibp_convert` : convert a text file into a binary one

`./build/gcc/release/hibp_sort`    : sort a binary file using external disk space (takes 3x space on disk)!

### Future uses

- I was considering adding a php/pyhton/javascript extension so that queries can be trivially made from within those scripting environments
