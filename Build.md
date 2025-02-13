# VitaOxiPad Client and server build instruction

This guide describes how to compile the VitaOxiPad Client and Server. It is assumed that the user has basic skills in using the console.

- [VitaOxiPad Client and server build instruction](#vitaoxipad-client-and-server-build-instruction)
  - [1. Client compilation](#1-client-compilation)
    - [1.1 Dependencies](#11-dependencies)
      - [1.1.1 Cargo, Make and Cmake](#111-cargo-make-and-cmake)
      - [1.1.2 flatc compilation](#112-flatc-compilation)
    - [1.2 Clone and compile client](#12-clone-and-compile-client)
  - [2. PS Vita server compilation](#2-ps-vita-server-compilation)

## 1. Client compilation

### 1.1 Dependencies

To compile the Client you will need

- [Make](https://www.gnu.org/software/make) and [Cmake](https://cmake.org);
- [Flatbuffers (flatc)](https://github.com/google/flatbuffers);
- [Rust](https://www.rust-lang.org/learn) ([cargo](https://doc.rust-lang.org/cargo)).

Install the necessary dependencies in a way that is convenient for you.

#### 1.1.1 Cargo, Make and Cmake

**For Windows:**

You can use [MSYS2](https://www.msys2.org), which provides this in a convenient way.

Dependencies you will need for **MSYS2 MINGW64**:

```bash
pacman -S mingw-w64-x86_64-rust make git wget cmake
```

**For Linux:**

For Alpine:

```bash
sudo apk add build-base cargo
```

For Arch:

```bash
sudo pacman -S base-devel rust
```

For Fedora:

```bash
sudo dnf install make automake gcc gcc-c++ cargo
```

#### 1.1.2 flatc compilation

You need to build [`flatc` v24.3.25](https://flatbuffers.dev/flatbuffers_guide_building.html) of a certain version from source code.

```bash
git clone --branch v24.3.25 https://github.com/google/flatbuffers.git && cd flatbuffers
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
make -j
./flattests # this is quick, and should print "ALL TESTS PASSED"
sudo make install
sudo ldconfig
flatc --version # "flatc version v24.3.25"
```

For Linux:

```bash
sudo make install
sudo ldconfig
```

For Windows MSYS2:

```bash
make install
```

And at the end check the `flatc` version:

```bash
flatc --version # "flatc version v24.3.25"
```

### 1.2 Clone and compile client

Clone the repository and log into it:

```bash
git clone --recurse-submodules https://github.com/DvaMishkiLapa/VitaOxiPad.git
cd ./VitaOxiPad
```

Build the client side:

```bash
cd ./client
cargo build --release --bin cli
```

You can check the finished binary by running in it:

```bash
 ./target/release/cli.exe your_PS_Vita_IP
```

## 2. PS Vita server compilation

1. Make sure that you have [`cmake`](https://cmake.org) installed;
2. Make sure you have [VitaSDK](https://vitasdk.org) installed and configured (try [vdpm](https://github.com/vitasdk/vdpm));
3. Sometimes, for whatever reason, `flatbuffers` are not installed in the VitaSDK (`fatal error: flatbuffers/flatbuffers.h: No such file or directory`).
  You can install it manually via vdpm. Do this after installing the VitaSDK via vdpm:

  ```bash
  ./vdpm flatbuffers
  ```

4. You should have [`flatc` v24.3.25](https://flatbuffers.dev/flatbuffers_guide_building.html) installed as in the paragraph
  [1.1.2 flatc compilation](#112-flatc-compilation), but tailored to where you want to compile the server part;

5. Build the project with the following commands:

  ```bash
  cmake -S server -B build
  cmake --build build
  ```

Then, install the generated `VitaOxiPad.vpk` file on your PS Vita.
