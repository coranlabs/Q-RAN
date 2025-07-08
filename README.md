<table>
  <tr>
    <td><img src="images/coranlabs-logo.png" alt="Q-RAN Logo" width="80"/></td>
    <td><h1 style="margin-left: 10px;">Q-RAN: Quantum Secure O-RAN</h1></td>
  </tr>
</table>

**Q-RAN** is a made for research, post-quantum secure upgrade for the 5G Open RAN (O-RAN) architecture.  
It secures all critical RAN interfaces using modern quantum-safe cryptography and is designed for future-proof telecom research.

---

## What It Does

- Secures O-RAN interfaces (F1, E1, N2, N3) using **Post-Quantum DTLS 1.3**
- Supports **CU-DU split** architecture with **real radios (7.2 split)**
- Runs in **real-time** on Linux servers with **hardware-synced timing**
- Built for **research, testing, and future-secure telecom deployments**
---
## Q-RAN Architecture
![Q-RAN Architecture](./docs/architecture.png)
---

## Why Q-RAN?

- **Post-Quantum Safe** — uses NIST-standard ML-KEM, ML-DSA  
- **Easy to Deploy** — single click deployment and execution  
- **Enhanced User Experience** — Enterprise grade logging mechanism and logs file generation  
- **Made with community- for community** — based on Opensource projects
- **Built for the future** — hybrid PQ + classical crypto, real-time kernel, upcoming GPU acceleration

 **Check out the [Release Notes](docs/release-notes.md)** for a full breakdown of features, fixes etc.

---

## Cryptography Details

### Key Exchange

| Mode  | Algorithms                      |
|-------|----------------------------------|
| Pure  | ML-KEM-768                      |
| Hybrid| X25519-MLKEM768 |

### Signatures

| Mode  | Algorithms                      |
|-------|----------------------------------|
| Pure  | ML-DSA-65            |
| Hybrid| Ed448-ML-DSA-65 |


### Symmetric Ciphers

- AES-256-GCM  
- AES-256-CTR  
- AES-256-CCM *(CTR + CBC-MAC)*

---


## Logs Demo

<p align="center">
  <img src="images/logs_demo.gif" alt="Q-RAN Logs Scroll" width="95%"/>
</p>

> Real-time PQ handshake and DTLS logs from live Q-RAN ARIA Deployment

---
## Test Bed
![Q-RAN Architecture](./docs/test_bed.png)

## Testbed Components

| **Layer**         | **Component**            |
|-------------------|--------------------------|
| **User Equipment (UE)** | Commercial Off-the-Shelf (COTS) UE, Quectel Modems |
| **Radio Unit (RU)** | LiteON O-RU              |
| **Grandmaster Clock** | Fibrolan Falcon RX |
| **Core Network**  | [QORE: Post-Quantum 5G Core](https://github.com/CoranLabs/QORE) |

## Technologies Used

Q-RAN is built on a modular and standards-aligned architecture, and brings together several open-source projects towards a superior Goal.

We acknowledge and appreciate the broader open-source community:

- **[OAI](https://openairinterface.org/)** — as an architectural base for ran stack
- **[QORE](https://github.com/CoranLabs/QORE/)** — Supported post Quantum safe 5G Core
- **[OSC](https://github.com/o-ran-sc)** — for the xRAN library (7.2 split support) 
- **[OpenSSL](https://www.openssl.org/)** — for secure TLS/DTLS foundation  
- **[liboqs](https://github.com/open-quantum-safe/liboqs)** & **[oqs-provider](https://github.com/open-quantum-safe/oqs-provider)** — for quantum-safe crypto integration  
 

---
## Documentation

For architecture diagrams, deployment guides, interface specs, and configuration examples, see the [**Q-RAN Docs**](docs/README.md).

---

## Planned for Release 2.0

- **Full IPSec support** across all interfaces  
- **GPU Offloading** For accelerating PQ-DTLS  with **CUPQC** on NVIDIA GPU
- **HQC** Additional post-quantum key encapsulation mechanism under NIST consideration
- **Falcon**  Compact and fast lattice-based digital signature scheme designed for constrained devices

---
## Contribution & Support

We welcome research collaboration, issue reports, and integration ideas.

📩 **Reach us at:** [contact@coranlabs.com](mailto:contact@coranlabs.com)
---
<p align="center">
  <a href="https://coranlabs.com" target="_blank">coranlabs.com</a>
</p>
