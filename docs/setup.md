<table>
  <tr>
    <td><img src="../images/coranlabs-logo.png.png" alt="Q-RAN Logo" width="50"/></td>
    <td><h1 style="margin-left: 10px;">Setting Up Server for Q-RAN</h1></td>
  </tr>
</table>


### 1 BIOS Settings

Make the following changes in the BIOS before proceeding:

* **Turbo Boost**: ON
* **CPU Enhanced Halt State (C1E)**: DISABLED
* **Hyper-Threading**: OFF
* **SR-IOV**: ENABLED


### 2 Real-Time Kernel

Install the Real-Time Kernel using Canonical’s `pro` subscription:

```bash
sudo pro attach <token>
sudo pro enable realtime-kernel
sudo reboot
```

After reboot, verify kernel:

```bash
uname -r
```

Expected Output:

```
5.15.0-1054-realtime
```

---

### 3 CPU Configuration

**Recommended CPU Allocation for Real-Time Operation**


#### 3.1 GRUB Configuration

Edit `/etc/default/grub` and add the following to `GRUB_CMDLINE_LINUX`:

```
intel_iommu=on iommu=pt mitigations=off mce=off idle=poll hugepagesz=1G hugepages=40 hugepagesz=2M hugepages=0 default_hugepagesz=1G selinux=0 enforcing=0 nmi_watchdog=0 softlockup_panic=0 audit=0 skew_tick=1 rcu_nocb_poll kthread_cpus=14-17 skew_tick=1 isolcpus=managed_irq,domain,0-13 nohz_full=0-13 rcu_nocbs=0-13 intel_pstate=disable nosoftlockup tsc=nowatchdog
```

Then update grub and reboot:

```bash
sudo update-grub
sudo reboot
```

Post-reboot, verify with:

```bash
cat /proc/cmdline
```

You should see the exact kernel boot parameters as entered above.

---

#### 3.2 Set CPU Governor to Performance

```bash
sudo apt install linux-tools-$(uname -r)
sudo cpupower frequency-set --governor performance
```

---

### 4 Configure Tuned Profile

Install and activate the real-time tuned profile:

```bash
sudo apt install tuned
```

Update `/etc/tuned/realtime-variables.conf`:

```ini
isolated_cores=0-13

# Optional: Uncomment to move IRQs from isolated cores
# isolate_managed_irq=Y
```

Activate the profile:

```bash
sudo tuned-adm profile realtime
```
<hr>

<p align="center">
  <a href="https://coranlabs.com" target="_blank">coranlabs.com</a>
</p>
