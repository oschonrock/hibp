# hibp
Have I been pwned database: High performance downloader, query tool, server and utilities

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

install the patch provided in bug report above into both versions of libstdc++ now install on the machine

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
```bash
# replace 'password' as you wish

./build/gcc/release/hibp_search hibp_all.bin 'password'
# output should be 
search took                0.2699 ms
needle = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:-1
found  = 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8:10434004
```
Performance will be mainly down to your disk and be around 15-20ms per uncached query, and <0.2ms cached.

### Running a server

You can run a high performance server for "pawned password queries" as follows:
```bash
./build/gcc/release/hibp_server hibp_all.bin
curl http://localhost:8082/password

# output should be:
count=10434004
```
Performance should be > 100 requests/second for a single core with zero latency and max concurrency but is highly disk dependent. 
Performance feedback on different system is very welcome. 
