# Building on Linux

## Recent Linux systems from the last 5 years

Explicitly tested on:
Debian 11 & 12, Ubuntu 20.04LTS, 22.04LTS & 24.04LTS.

On rpm system, the package names may vary slightly. The only runtime
dependencies are libcurl and libevent (plus libtbb if you compile with
`-DHIBP_WITH_PSTL`).

## Install Dependencies

```bash
sudo apt install build-essential cmake curl ninja-build ccache git libcurl4-openssl-dev libevent-dev ruby libtbb-dev
git clone https://github.com/oschonrock/hibp.git
cd hibp
git submodule update --init --recursive
cd ext/restinio
sudo gem install Mxx_ru   # install ruby gem required for restinio dependency installation
mxxruexternals            # install those deps
cd ../..

# optional: for compiling with clang also:
sudo apt install clang gcc-14 g++-14  # need gcc-14 because clang tries to use its stdlibc++ version
```
