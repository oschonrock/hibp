# FreeBSD 14.1

## Install Dependencies

```bash
# as root
pkg install bash getopt cmake ccmake ccache git ruby ruby32-gems rubygem-rake curl libevent onetbb 
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
