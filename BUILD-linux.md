### Ubuntu 24.04

#### Install Dependencies
```bash
sudo apt install build-essential ninja-build git cmake libcurl4-openssl-dev libevent-dev ruby libtbb-dev
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
This is only important for hibp_sort, which is mostly deprecated
