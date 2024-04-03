# lwebsrv
An http/https server, with its own simple template format aiming for easy interoperability with your C code.
This repository comes an example page.
All configuration is done in the source code before building.

## Build

### Requirements
- GCC or clang
- GNU make
- OpenSSL

Clone the repository, then run make. Use `DEBUG=1` to build with debug symbols, as well as ASan and UBSan.
Binaries are installed to `/usr/local/bin/` by default.

```
git clone --recursive https://lutfisk.net/git/lwebsrv/.git
sudo make install
```
