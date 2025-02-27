# Build with MSYS2 / mingw

Download and install MSYS2 with all default settings
https://www.msys2.org/

Open the "gold" MSYS2 UCRT64 console from start menu (always use this
one in future):

```bash
pacman -S pactoys git unzip
pacboy -S gcc ccache cmake ccmake ruby tbb curl libevent diffutils

git clone https://github.com/oschonrock/hibp.git
cd hibp
git submodule update --recursive --init

cd ext/restinio
gem install Mxx_ru   # install ruby gem required for restinio dependency installation
mxxruexternals       # install those dependencies
cd ../..
```

and continue with main README...

## Running executables / loading shared libraries

By default this project builds with as many static libraries as
possible, but you still need to ensure the remaining dynamic ones can
be found. Note that `PATH` is sensibly configured within the MSYS2
environment, and executables should just run trivially there. But the
same will not necessarily be true in the Windows `cmd` console, or
when launching from File Explorer, etc.

To be able to run the programs in the build directory from the `cmd`
(windows shell) you need to put `C:\msys64\ucrt64\bin` into your `PATH`
environment variable. You can do that in the usual place in the
Windows GUI.

Any problems and you can check what each program is loading with
`ldd program_name` from within the MSYS2 console.
