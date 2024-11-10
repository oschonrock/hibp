# Build with MSYS2 / mingw 

Download and install MSYS2 with all default
https://www.msys2.org/

Open the "gold" MSYS2 UCRT64 console from start menu (always use this one in future)

```bash
pacman -S pactoys git unzip
pacboy -S gcc cmake ccmake ruby tbb curl libevent

git clone https://github.com/oschonrock/hibp.git
cd hibp
git submodule update --recursive --init

cd ext/restinio
gem install Mxx_ru   # install ruby gem required for restinio dependency installation
mxxruexternals       # install those dependencies
cd ../..
```

And continue with testing / usage phase in the main [README](README.md).

Also refer to BUILD-linux.md for patching libstdc++
haven't worked out the exact commands for Windows.
But this is only important for hibp_sort, which is mostly deprecated

## clang

You can also build with `clang`, by install it in the UCRT64 environment.
```
pacboy -S clang
```

### Using sanitizers

`clang` supports sanitizers under mingw, but right now, it is only
easy to do if you use the clang64 MSYS2 environment, which installs a
more complete set of libs with the compiler-rt package.

in MSYS2 clang64:
```
pacman -S pactoys git unzip
pacboy -S clang cmake ccmake ruby tbb curl libevent
```

and then use `-fsanitize=adrress,undefined` (leak is not
supported). You will need to edit CMakeLists.txt, as sanitizers are
normally disabled for mingw/MSYS2. 

## Running executables / loading shared libraries

By default this project builds with as many static libraries as
possible, but you still need to ensure all can be found. Note the
paths are normally sensibly configured with the MSYS2 environment, and
executables should just run trivially there. But the same will not
necessarily be true in the Windows `cmd` console, or when luanching
from File Explorer, etc.

Check what each program is loading with `ldd program_name` from within
the MSYS2 console. 

To be able to run the programs in the build directory from the `cmd` (windows shell) 
you need to put `C:\msys64\ucrt64\bin` (or other paths you found with
`ldd`) into your `PATH` environment variable. You can do that in the
usual place in the Windows GUI. 
