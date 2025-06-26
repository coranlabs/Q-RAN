<table>
  <tr>
    <td><img src="../assets/2.png" alt="Q-RAN Logo" width=100"/></td>
    <td><h1 style="margin-left: 10px;">Running Q-RAN</h1></td>
  </tr>
</table>

---

Once the build is complete and configuration files are in place, Q-RAN can be executed using two separate binaries for the CU and DU.

### Step 1: Start the CU (Layer 3 Stack)

```bash
sudo ./CU_QRAN_start
```


### Step 2: Start the DU (Layer L1-L2 Stack)

```bash
sudo ./DU_QRAN_start
```
---

## Runtime Logging

During execution, logs are streamed in the terminal and also saved to files for later inspection.

### Sample L3 Log Output

> Example log lines from the CU side
```


       █████╗  ██████╗  ██╗ █████╗              ██╗     ██████╗      
      ██╔══██╗ ██╔══██╗ ██║██╔══██╗             ██║     ╚════██╗     
      ███████║ ██████╔  ██║███████║    █████    ██║      █████╔╝     
      ██╔══██║ ██╔══██╗ ██║██╔══██║             ██║      ╚═══██╗     
      ██║  ██║ ██║  ██║ ██║██║  ██║             ███████╗██████╔╝     
      ╚═╝  ╚═╝ ╚═╝  ╚═╝ ╚═╝╚═╝  ╚═╝             ╚══════╝╚═════╝      
    ARIA STARTED
[2025-07-12T07:26:15] [ INFO ] [ RRC   ] RRC Instance 0: Establishing Southbound Transport Layer with local MAC address binding.
[2025-07-12T07:26:15] [ INFO ] [ ARIA  ] TASK_SCTP : Initiating the thread creation procedure
[2025-07-12T07:26:15] [ INFO ] [ ARIA  ] TASK_NGAP : Initiating the thread creation procedure
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Initiating NGAP Layer
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] NGAP Layer recieved Registration Request
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Initilizaing Detection For New gNB:  Successful 
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Initiating validation of AMF registration state
[2025-07-12T07:26:15] [ INFO ] [ ARIA  ] TASK_GNB_APP : Initiating the thread creation procedure
[2025-07-12T07:26:15] [DEBUG ] [ SCTP  ] Received association change notification. Processing state change.
[2025-07-12T07:26:15] [ INFO ] [ SCTP  ] New association established: In Streams=2, Out Streams=2
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Building NGAP Setup Request.
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Sending NGAP Setup Request.
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Successfully encoded Message: [21]
[2025-07-12T07:26:15] [DEBUG ] [ NGAP  ] Forwarding NGAP Setup Response to SCTP Task.
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] NGAP Setup Response Transmisson Status [from CU]: Successfull
[2025-07-12T07:26:15] [ INFO ] [ SCTP  ] Sending the message  :
00 15 00 38 00 00 04 00  1B 00 09 00 00 F1 10 30 
00 00 E0 00 00 52 40 0E  05 80 43 6F 72 61 6E 4C 
61 62 73 2D 43 55 00 66  00 0D 00 00 00 00 01 00 
00 F1 10 00 00 00 08 00  15 40 01 00 
[2025-07-12T07:26:15] [DEBUG ] [ SCTP  ] Successfully sent  60 bytes on stream 0 for assoc_id 1863

[2025-07-12T07:26:15] [ INFO ] [ ARIA  ] TASK_CU_F1 : Initiating the thread creation procedure
[2025-07-12T07:26:15] [ INFO ] [ ARIA  ] TASK_RRC_GNB : Initiating the thread creation procedure
[2025-07-12T07:26:15] [DEBUG ] [ SCTP  ] Message received: Length=53, Stream=0
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Decoding NG Setup Message.
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] Handling NGAP Message, PDU decoding success
[2025-07-12T07:26:15] [ INFO ] [ NGAP  ] NGAP Setup Response Message received.
[2025-07-12T07:26:15] [ INFO ] [ F1AP  ] Starting the F1AP CU task.
[2025-07-12T07:26:15] [ INFO ] [ GTPU  ] Initiating GTP-U configuration: Setting up GTP-U interface parameters for tunneling protocol.
[2025-07-12T07:26:15] [ INFO ] [ F1AP  ] Started TASK_CU_F1 1

[2025-07-12T07:26:15] [ INFO ] [ GTPU  ] Initiating GTP-U address configuration sequence: Setting up local address for GTP-U data tunneling.
[2025-07-12T07:26:15] [ INFO ] [ F1AP  ] Sending sctp init request
[2025-07-12T07:26:15] [ INFO ] [ GTPU  ] Initializing UDP socket for local address binding
[2025-07-12T07:26:15] [ INFO ] [ RRC   ] Initiating primary execution loop for handling of the task of RRC 
SCTP_INIT_MSG
[2025-07-12T07:26:15] [ INFO ] [ F1AP  ] Sent sctp init request
[2025-07-12T07:26:15] [ INFO ] [ F1AP  ] Sent SCTP initialization request.
[2025-07-12T07:26:15] [DEBUG ] [ SCTP  ] Configurations: bind_address=127.0.0.4, port=38472
[2025-07-12T07:26:15] [ INFO ] [ GTPU  ] Initializing UDP socket for local address binding
[2025-07-12T07:26:15] [DEBUG ] [ SCTP  ] Address resolution for bind_address=127.0.0.4, port=38472 completed
[2025-07-12T07:26:15] [ INFO ] [ GTPU  ] Successfully instantiated GTP-U interface instance
[2025-07-12T07:26:15] [ INFO ] [ GTPU  ] Successfully instantiated GTP-U interface instance
[2025-07-12T07:26:15] [DEBUG ] [ SCTP  ] SCTP initialization options set: In Streams=1, Out Streams=1
[2025-07-12T07:26:15] [ INFO ] [ RRC   ]  Connection Protocol: Acknowledging and securing new Central Unit User Plane (CU-UP) session. Initializing handshake and session parameters
[2025-07-12T07:26:15] [ INFO ] [2025-07-12T07:26:15] [ ARIA  ] [ INFO ] TASK_GTPV1_U : Initiating the thread creation procedure
[ SCTP  ] setsockopt returns success, Socket Descriptor : [96]
[2025-07-12T07:26:15] [ INFO ] [ SCTP  ] Successfully created and bound listening socket on IP=127.0.0.4, port=38472
[2025-07-12T07:26:15] [DEBUG ] [ SCTP  ] Allocated memory for SCTP connection structure
[2025-07-12T07:26:15] [ INFO ] [DTLS   ] Library context loaded.

[2025-07-12T07:26:15] [ INFO ] [DTLS   ] OSSL_PROVIDER available

[2025-07-12T07:26:15] [ INFO ] [DTLS   ] Provider name: oqsprovider

[2025-07-12T07:26:15] [ INFO ] [DTLS   ] OSSL provider - oqs loaded.

[2025-07-12T07:26:15] [ INFO ] [DTLS   ] Server CTX

✅ SSL_OP_NO_TICKET is set


```


### Sample L1 + L2 Log Output

> Example log lines from the DU side

```

       █████╗  ██████╗  ██╗ █████╗               ██╗     ██╗      ██╗     ██████╗ 
      ██╔══██╗ ██╔══██╗ ██║██╔══██╗              ██║    ███║      ██║     ╚════██╗
      ███████║ ██████╔  ██║███████║    █████     ██║    ╚██║█████ ██║      █████╔╝
      ██╔══██║ ██╔══██╗ ██║██╔══██║              ██║     ██║      ██║     ██╔═══╝ 
      ██║  ██║ ██║  ██║ ██║██║  ██║              ███████╗██║      ███████╗███████╗
      ╚═╝  ╚═╝ ╚═╝  ╚═╝ ╚═╝╚═╝  ╚═╝              ╚══════╝╚═╝      ╚══════╝╚══════╝ 
    ARIA STARTED
[2025-07-12T07:26:22] [ INFO ] [MAC    ] PARAMETERS ARE CONFIGURED. ASSIGNING SHARED STRUCTURES FOR COMMUNICATION BETWEEN LAYERS
[2025-07-12T07:26:22] [ INFO ] [ ARIA  ] LAYER 1 Parameters: Transmission Amplitude: 3276 , Receiving Thread Cores: 8
[2025-07-12T07:26:22] [ INFO ] [ PHY   ] NORTHBOUND INTERFACE FOR LAYER 1 PREPARED
[2025-07-12T07:26:22] [ INFO ] [ PHY   ] POSITIONING CONFIGURATION PROCEDURE STATUS: FAILED ToA And AoA Configuration Not Found.
[2025-07-12T07:26:22] [ INFO ] [ PHY   ] Task: Initializing structures....

[2025-07-12T07:26:22] [ INFO ] [ PHY   ] Structure Initialization Successful
[2025-07-12T07:26:22] [ INFO ] [MAC    ] Important Metrics: DUPLEX MODE & SPACING: TDD , 0. Using NR Band 78
[2025-07-12T07:26:22] [ INFO ] [MAC    ] Important Metrics: DUPLEX MODE & SPACING: TDD , 0. Using NR Band 78
[2025-07-12T07:26:22] [ INFO ] [MAC    ] Important Metrics: DUPLEX MODE & SPACING: TDD , 0. Using NR Band 78
[2025-07-12T07:26:22] [ INFO ] [MAC    ] PUSCH LIMITS: GOAL: 200, Error: 10
[2025-07-12T07:26:22] [ INFO ] [MAC    ] PUCCH LIMITS: GOAL: 150, Error: 10
[2025-07-12T07:26:22] [ INFO ] [MAC    ] Important Metrics: DUPLEX MODE & SPACING: TDD , 0. Using NR Band 78
[2025-07-12T07:26:22] [ INFO ] [MAC    ] Other Metrics: ssb OffSetPointA: 252, ssb_SubCarrierOffest: 12
[2025-07-12T07:26:22] [ INFO ] [MAC    ] ANTENNA INFORMATION: RX NUMBER: 2, TX NUMBER: 4
[2025-07-12T07:26:22] [ INFO ] [ PHY   ] Initialization Parameters: DL Resource Blocks: 273 | Carrier Offset: $d | CP Samples (Normal): 2458 | CP Samples (First): 288 | OFDM Symbol FFT Size: 352.

[2025-07-12T07:26:22] [ INFO ] [MAC    ] Important Metrics: DUPLEX MODE & SPACING: TDD , 0. Using NR Band 78
[2025-07-12T07:26:22] [ INFO ] [ ARIA  ] F1AP CRITICAL PARAMETERS ARE SET AS: MCC 1, MNC 1 TAC 1
[2025-07-12T07:26:22] [ INFO ] [GNB_APP] Detected Cell Configured according to: TDD
[2025-07-12T07:26:22] [ INFO ] [GNB_APP] SDAP LAYER STATUS:isDISABLED
[2025-07-12T07:26:22] [ INFO ] [ ARIA  ] TASK_SCTP : Initiating the thread creation procedure
[2025-07-12T07:26:22] [ INFO ] [ ARIA  ] TASK_GNB_APP : Initiating the thread creation procedure
[2025-07-12T07:26:22] [ INFO ] [ ARIA  ] TASK_DU_F1 : Initiating the thread creation procedure
[2025-07-12T07:26:22] [ INFO ] [ ARIA  ] TASK_GTPV1_U : Initiating the thread creation procedure
[2025-07-12T07:26:22] [ INFO ] [ GTPU  ] Initializing UDP socket for local address binding
[2025-07-12T07:26:22] [ INFO ] [ GTPU  ] Successfully instantiated GTP-U interface instance
[2025-07-12T07:26:22] [ INFO ] [DTLS   ] Library context loaded.

[2025-07-12T07:26:22] [ INFO ] [DTLS   ] OSSL_PROVIDER available

[2025-07-12T07:26:22] [ INFO ] [DTLS   ] Provider name: oqsprovider

[2025-07-12T07:26:22] [ INFO ] [DTLS   ] OSSL provider - oqs loaded.

[2025-07-12T07:26:22] [ INFO ] [ PHY   ] Awaiting full configuration of all Layer 1 instances. Operational status: Idle  
 

[2025-07-12T07:26:22] [DEBUG ] [DTLS   ] CA Certificate loaded.

-------------------DU CERTIFICATE-------------------------

```

###  UE stats logs :
<img width="914" height="744" alt="UE_Stats" src="../assets/UE_Stats.png" />

---

## Log File Output

After termination (via Ctrl+C or exit), all logs are saved to:

```bash
<project-directory>/logs/
```

Sample:

```bash
ls logs/
aria_l1_l2_logs_file.txt  aria_l3_logs_file.txt
```

These logs respect the YAML-based logging configuration:

* Turn on/off logs per component (e.g. RRC, F1AP, XRAN)
* Control verbosity (DEBUG, INFO, WARN)
* Separate control for terminal and file output

---

<p align="center">
  <a href="https://coranlabs.com" target="_blank">coranlabs.com</a>
</p>
