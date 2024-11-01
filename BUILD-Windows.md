# Build with MSYS2 / mingw 

Download and install MSYS2 with all default
https://www.msys2.org/

Open the "gold" MSYS2 UCRT64 console from start menu (always use this one in future)

```bash
pacman -S pactoys git unzip
pacboy -S gcc:p cmake:p ccmake:p ruby:p tbb:p curl:p libevent:p

git clone https://github.com/oschonrock/hibp.git
cd hibp
git submodule update --recursive --init

cd ext/restinio
gem install Mxx_ru   # install ruby gem required for restinio dependency installation
mxxruexternals       # install those dependencies
cd ../..
```

And continue with testing / usage phase in main README
