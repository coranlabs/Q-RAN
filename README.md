<table>
  <tr>
    <td><img src="images/coranlabs-logo.png" alt="Q-RAN Logo" width="80"/></td>
    <td><h1 style="margin-left: 10px;">Q-RAN: Quantum-Resilient O-RAN Architecture</h1></td>
  </tr>
</table>

**Q-RAN** is a quantum-resistant security framework for 5G Open RAN (O-RAN) architecture, implementing NIST-standardized Post-Quantum Cryptography across all critical RAN interfaces. Built on OpenAirInterface5G with support for commercial O-RU (7.2 split) and real-time execution, Q-RAN secures fronthaul, midhaul, and backhaul communications using ML-KEM, ML-DSA, and QRNG.

---

## Research Publication

**"Q-RAN: Quantum-Resilient O-RAN Architecture"**  
*Vipin Rathi, Lakshya Chopra, Madhav Agarwal, Nitin Rajput, Kriish Sharma, Sushant Mundepi, Shivam Gangwar, Rudraksh Rawal, Jishan*

arXiv: [https://arxiv.org/abs/2510.19968](https://arxiv.org/abs/2510.19968) | PDF: [https://arxiv.org/pdf/2510.19968](https://arxiv.org/pdf/2510.19968)

**Submitted**: October 22, 2025 | **Pages**: 23

```bibtex
@article{qran2024,
  title={Q-RAN: Quantum-Resilient O-RAN Architecture},
  author={Rathi, Vipin and Chopra, Lakshya and Agarwal, Madhav and Rajput, Nitin and Sharma, Kriish and Mundepi, Sushant and Gangwar, Shivam and Rawal, Rudraksh and Jishan},
  journal={arXiv preprint arXiv:2510.19968},
  year={2024}
}
```

---

## Overview

Q-RAN addresses quantum computing threats to disaggregated O-RAN architectures by deploying **PQ-IPsec**, **PQ-DTLS**, and **PQ-mTLS** across F1, E1, E2, N2, and N3 interfaces. The framework includes a centralized Post-Quantum Certificate Authority (PQ-CA) within the SMO framework, ensuring end-to-end quantum security from Radio Unit to Core Network.

### Key Features

- **Post-Quantum Safe**: NIST-standardized ML-KEM (FIPS 203) and ML-DSA (FIPS 204)
- **Comprehensive Coverage**: All O-RAN interfaces (F1, E1, E2, N2, N3)
- **CU-DU Split**: Supports real radios (7.2 split) with hardware-synced timing
- **Hybrid Mode**: Classical + PQ crypto for smooth migration
- **QRNG Integration**: True quantum entropy for key generation
- **Enterprise Logging**: Advanced logging with comprehensive file generation
- **Production-Ready**: Real-time kernel, commercial O-RU support

**Check the [Release Notes](docs/release-notes.md)** for features and updates.

---

## The Quantum Threat to O-RAN

O-RAN's disaggregated architecture creates unique quantum security challenges:

- **Expanded Attack Surface**: Multiple interfaces require independent security
- **HNDL Attacks**: Adversaries capture RAN traffic for future quantum decryption
- **Long Deployment Cycles**: Equipment operates 10-20 years
- **Critical Data**: Subscriber information, location, authentication

**Standards Response**: 3GPP SA3, O-RAN Alliance working groups, NIST PQC (FIPS 203/204/205)

---

## Q-RAN Architecture

![Q-RAN Architecture](./docs/architecture.png)

### Components

**Radio Unit (RU)**: Fronthaul (7.2 split) secured with PQ-DTLS, commercial O-RU support (LiteON)  
**Distributed Unit (DU)**: F1 to CU with PQ-DTLS, real-time L1/L2 processing  
**Central Unit (CU)**: CU-CP (PQ-DTLS for F1-C/E1), CU-UP (PQ-IPsec for N3)  
**SMO**: Centralized PQ-CA, O1 (PQ-mTLS), E2 (PQ-IPsec), QRNG integration  
**Core Integration**: Seamless with [QORE: Post-Quantum 5G Core](https://github.com/CoranLabs/QORE)

---

## Post-Quantum Cryptography

### Primitives

**ML-KEM (Module-Lattice Key Encapsulation)**
- Standard: FIPS 203 | Algorithm: MLWE
- Levels: 512/768/1024 (AES-128/192/256 equivalent)
- Performance: 236,000 key exchanges/sec (ML-KEM-768)
- Use: DTLS key exchange, IPsec IKEv2

**ML-DSA (Module-Lattice Digital Signature)**
- Standard: FIPS 204 | Algorithm: MSIS
- Levels: 44/65/87 (AES-128/192/256 equivalent)
- Performance: 1.15M signature verifications/sec
- Use: Certificates, authentication

### Hybrid Mode

| Mode | Key Exchange | Signatures |
|------|--------------|------------|
| Pure PQC | ML-KEM-768 | ML-DSA-65 |
| Hybrid PQC | X25519-MLKEM768 | Ed448-ML-DSA-65 |

**Benefits**: Security if PQC breaks, interoperability, smooth migration

### Symmetric Encryption

- AES-256-GCM: Authenticated encryption for IPsec
- AES-256-CTR: High-performance streaming
- AES-256-CCM: CTR + CBC-MAC for constrained devices

---

## Security Protocols

**PQ-DTLS 1.3**: RAN control plane (F1, E1, N2) with ML-KEM + ML-DSA, UDP with fragmentation  
**PQ-IPsec**: User plane (N3, E2) with IKEv2 + ML-KEM, ESP AES-256-GCM  
**PQ-mTLS 1.3**: Management (O1) with ML-KEM + ML-DSA, TCP/HTTP/2

---

## O-RAN Interface Security

| Interface | Protocol | Classical | Q-RAN (Post-Quantum) | Status |
|-----------|----------|-----------|----------------------|--------|
| Fronthaul (7.2) | eCPRI | No encryption | PQ-DTLS 1.3 | Completed |
| F1-C | SCTP | DTLS 1.2 (ECDHE) | PQ-DTLS 1.3 (ML-KEM) | Completed |
| F1-U | GTP-U | IPsec (DH) | PQ-IPsec (ML-KEM) | In Progress |
| E1 | SCTP | DTLS 1.2 | PQ-DTLS 1.3 | Completed |
| E2 | SCTP | IPsec (optional) | PQ-IPsec | Planned |
| N2 | SCTP | DTLS 1.2 | PQ-DTLS 1.3 | Completed |
| N3 | GTP-U | IPsec | PQ-IPsec | Completed |
| O1 | HTTP/2 | mTLS (RSA/ECDSA) | PQ-mTLS (ML-DSA) | Completed |

**Fronthaul Challenge**: Ultra-low latency (<250 μs), high throughput (10+ Gbps)  
**Solution**: PQ-DTLS for C-Plane, hardware timestamping, upcoming GPU acceleration

---

## Post-Quantum Certificate Authority

Centralized PQ-CA within SMO framework:

- **Root CA**: ML-DSA-87 (long-term security)
- **Intermediate CAs**: Per-operator hierarchy
- **End-Entity**: ML-DSA-65 for RAN components (RU, DU, CU)
- **Revocation**: CRL/OCSP with quantum-safe signatures
- **Lifecycle**: Automated issuance, renewal, revocation

---

## Performance

### Cryptographic Operations

| Operation | Algorithm | Performance |
|-----------|-----------|-------------|
| Key Encapsulation | ML-KEM-768 | 236,000 ops/s |
| Sign Verification | ML-DSA-65 | 1,150,000 ops/s |

### DTLS Handshake

| Protocol | Handshake Time | Overhead |
|----------|---------------|----------|
| Classical DTLS 1.2 | 12-18 ms | Baseline |
| PQ-DTLS 1.3 | 18-25 ms | +6-7 ms |

**Real-Time**: Meets 1ms TTI, <30ms F1-C latency, <1μs sync accuracy

---

## Test Bed

![Q-RAN Test Bed](./docs/test_bed.png)

| Layer | Component | Details |
|-------|-----------|---------|
| UE | COTS UE | Quectel modems, smartphones |
| RU | LiteON O-RU | Commercial 7.2 split radio |
| DU/CU | Q-RAN | OpenAirInterface5G with PQ-DTLS |
| Clock | Fibrolan Falcon RX | PTP grandmaster |
| Core | [QORE](https://github.com/CoranLabs/QORE) | Post-Quantum 5G Core |

**Hardware**: Intel Xeon, 32GB+ RAM, Linux RT kernel, 10GbE fronthaul

---

## Logs Demo

<p align="center">
  <img src="images/logs_demo.gif" alt="Q-RAN Logs" width="95%"/>
</p>

> Real-time PQ handshake and DTLS logs from live Q-RAN deployment

---

## Getting Started

### Prerequisites

- Ubuntu 20.04/22.04 LTS with real-time kernel
- Intel Xeon/AMD EPYC, 32GB+ RAM
- Compatible O-RU (7.2 split), PTP grandmaster
- QORE or compatible 5G Core

### Quick Start

```bash
git clone https://github.com/coranlabs/Q-RAN.git
cd Q-RAN
./build_aria.sh

# Configure (edit configs/cu_config.yaml, configs/du_config.yaml)

./CU_QRAN_start        # Terminal 1
./DU_QRAN_start        # Terminal 2
tail -f logs/qran_*.log
```

**Detailed guides**: See [Q-RAN Documentation](docs/README.md)

---

## Technologies Used

- **[OpenAirInterface (OAI)](https://openairinterface.org/)**: RAN stack base
- **[QORE](https://github.com/CoranLabs/QORE/)**: Post-Quantum 5G Core
- **[O-RAN SC](https://github.com/o-ran-sc)**: xRAN library (7.2 split)
- **[liboqs](https://github.com/open-quantum-safe/liboqs)**: PQC algorithms
- **[wolfSSL](https://www.wolfssl.com/)**: Embedded SSL/TLS with PQC
- **[strongSwan](https://www.strongswan.org/)**: IPsec with PQC

---

## Roadmap

### Release 1.0 (Completed)
- PQ-DTLS 1.3 for F1, E1, N2
- ML-KEM-768, ML-DSA-65 integration
- Hybrid PQC mode
- QRNG, PQ-CA within SMO
- Commercial O-RU support (7.2 split)
- Real-time execution, enterprise logging
- QORE integration
- Published research paper (arXiv:2510.19968)

### Release 2.0 (Planned)
- Full PQ-IPsec (F1-U, N3, E2)
- GPU offloading (CUPQC on NVIDIA)
- Additional RAN stacks (O-RAN SC DU-High, srsRAN, SD-RAN)
- Post-Quantum SMO and RICs
- HQC, Falcon support

### Release 3.0 (Future)
- QKD integration
- AI-based threat detection
- Network slicing security
- Private 5G Q-RAN packages

---

## Publications

### Research Papers
- **Q-RAN: Quantum-Resilient O-RAN Architecture** - Rathi et al., 2024  
  arXiv:2510.19968 | [PDF](https://arxiv.org/pdf/2510.19968)

### Related Work
- **QORE: Quantum Secure 5G/B5G Core** - Rathi et al., 2024  
  arXiv:2510.19982 | [GitHub](https://github.com/CoranLabs/QORE)

### Whitepapers
- [coRAN Labs Whitepapers Repository](https://github.com/coranlabs/WhitePapers)

---

## Documentation

- [Architecture Overview](docs/architecture.md)
- [Deployment Guide](docs/deployment.md)
- [Interface Specifications](docs/interfaces.md)
- [Configuration Reference](docs/configuration.md)
- [Release Notes](docs/release-notes.md)

---

## License

**Q-RAN** is licensed under the **coRAN LABS Public License Version 1.0**.

- **Research/Academic Use**: Free, no restrictions
- **Commercial Use**: FRAND licensing required
- **Patent Grant**: Royalty-free for research

**Full License**: [LICENSE](LICENSE)

**Commercial Licensing**: contact@coranlabs.com

---

## Contact

### coRAN Labs
- **Website**: [www.coranlabs.com](https://www.coranlabs.com/)
- **Email**: contact@coranlabs.com
- **GitHub**: [github.com/coranlabs](https://github.com/coranlabs)

We welcome research collaboration, issue reports, and integration ideas.

---

<p align="center">
  <strong>Securing the Future of Wireless Networks</strong><br>
  <em>Q-RAN: Quantum-Resilient Radio Access for the Post-Quantum Era</em>
</p>

<p align="center">
  Copyright © 2024 coRAN Labs and Contributors<br>
  Licensed under coRAN LABS Public License v1.0
</p>
