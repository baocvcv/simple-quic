# THQUIC Project

Simple QUIC implementation for the Advanced Networking course.

# Prerequisite

* Cmake > 3.15
* spdlog
```shell
git submodule update --init --recursive
cd extern/spdlog && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=release -DCMAKE_INSTALL_PREFIX=$(pwd)/../install -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make && make install
```

# Build 

```shell
mkdir build && cd build 
cmake .. && make
```


RTT：https://tools.ietf.org/html/draft-ietf-quic-recovery-29#appendix-A