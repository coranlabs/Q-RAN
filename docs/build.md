<table>
  <tr>
    <td><img src="../assets/2.png" alt="Q-RAN Logo" width=100"/></td>
    <td><h1 style="margin-left: 10px;">Building Q-RAN</h1></td>
  </tr>
</table>

---
## Prerequisites 
To build and run the Q-RAN stack, a few dependencies must be installed and configured correctly. These include:

* **DPDK** (Data Plane Development Kit)
* **OpenSSL** (with post-quantum cryptographic support)
* **liboqs** and **oqs-provider** (from the Open Quantum Safe project)

---

### 1. Install System Packages and ASN.1 Compiler

The following script installs the required compiler toolchain and a specific version of the **ASN.1 compiler** (`asn1c`) used to parse and generate RAN protocol headers.

```bash
#!/bin/bash

SRC_DIR="/usr/local/src"
REPO_URL="https://github.com/mouse07410/asn1c.git"
REPO_NAME="asn1c"
COMMIT_HASH="a57bd1113f7148e23e1ff59e12d7484b838089a2"

# Create source directory
sudo mkdir -p "$SRC_DIR"
sudo chown "$USER":"$USER" "$SRC_DIR"

# Clone and build asn1c
cd "$SRC_DIR" || exit
git clone "$REPO_URL"
cd "$REPO_NAME" || exit
git checkout "$COMMIT_HASH"

sudo apt update
sudo apt install -y autoconf automake libtool gcc make bison flex

autoreconf -iv
./configure
make -j"$(nproc)"
sudo make install

# Additional development dependencies
sudo apt install -y \
    cmake \
    g++ \
    libssl-dev \
    pkg-config \
    libyaml-dev \
    libsctp-dev \
    libconfig-dev
```

---

### 2. Installing DPDK (Recommended Version: 20.11.9)

Q-RAN uses DPDK for high-performance, zero-copy packet processing in user space.

**Install prerequisites:**

```bash
sudo apt update
sudo apt install wget xz-utils libnuma-dev meson ninja-build
```

**Download and compile DPDK:**

```bash
cd ~
wget http://fast.dpdk.org/rel/dpdk-20.11.9.tar.xz
tar -xvf dpdk-20.11.9.tar.xz
cd dpdk-stable-20.11.9
meson build
ninja -C build
sudo ninja install -C build
```

**Verify installation:**

```bash
sudo ldconfig -v | grep rte_
```

Expected output (partial):

```
librte_fib.so.0.200.2 -> librte_fib.so.0.200.2
librte_telemetry.so.0.200.2 -> librte_telemetry.so.0.200.2
...
```

```bash
pkg-config --libs libdpdk --static
```

This should return a long list of `-lrte_*` libraries, indicating successful installation.

---

### 3. Setting Up PQC Libraries

Q-RAN integrates post-quantum cryptography using OpenSSL, liboqs, and oqs-provider. These libraries must be cloned and built in a specific location.

**Required path structure:**

All libraries must be placed inside the following directory:

```
<project-directory>/pqc/
```

**Expected contents:**

```
pqc/
├── CMakeLists.txt
├── dtls/
├── liboqs/
├── openssl/
└── oqs-provider/
```
---

### 4. Installing LinuxPTP (ptp4l and phc2sys)

Q-RAN relies on **S-Plane synchronization** for accurate timing. This is handled by `ptp4l` and `phc2sys` from the [LinuxPTP](https://github.com/richardcochran/linuxptp) project.

### Clone and Build LinuxPTP

```bash
cd ~
git clone https://github.com/richardcochran/linuxptp.git
cd linuxptp
make
sudo make install
```

### Verify Installation

```bash
ptp4l -V
phc2sys -V
```

Expected output:

```
ptp4l version 3.x
phc2sys version 3.x
```


---


## Building Q-RAN 

Once all dependencies are in place, build Q-RAN with the following command:

```bash
cd <project-directory>
sudo ./build_aria.sh
```
> This script builds the entire Q-RAN ARIA stack, including aria_l2_app and aria_l3_app.
    


---
<p align="center">
  <a href="https://coranlabs.com" target="_blank">coranlabs.com</a>
</p>
