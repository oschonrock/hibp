### Ubuntu 24.04

#### Install Dependencies
```bash
sudo apt install build-essential cmake ninja-build ccache git libcurl4-openssl-dev libevent-dev ruby libtbb-dev
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

