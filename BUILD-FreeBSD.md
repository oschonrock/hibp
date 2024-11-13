### FreeBSD 14.1 

#### Install Dependencies
```bash
# as root
pkg install getopt curl libevent onetbb cmake ccmake ccache git ruby ruby32-gems rubygem-rake
gem install Mxx_ru   # install ruby gem required for restinio dependency installation

# as normal user
cd
git clone https://github.com/oschonrock/hibp.git
cd hibp
git submodule update --init --recursive
cd ext/restinio
mxxruexternals            # install those restinio deps
cd ../..
```

#### Parallel STL algorithms (PSTL) not supported under libc++

code will fall back to single threaded, but this only affects `hibp_sort`

You can add `-fexperimental-library` in CMakeLists.txt and remove the 

`#if __cpp_lib_parallel_algorithm`

in include/flat_file.cpp

and that should compile and link OK for PSTL. 

