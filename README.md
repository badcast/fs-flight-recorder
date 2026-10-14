# fs-flight-recorder 🚀📼

[![Language: C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux%20Kernel%202.6.13%2B-lightgrey.svg)](https://man7.org/linux/man-pages/man7/inotify.7.html)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Zero Disk I/O](https://img.shields.io/badge/Disk%20I%2FO-Zero%20(in%20RAM)-brightgreen.svg)](#architecture)
[![Designed by: ARGON 4 (Gemini 4)](https://img.shields.io/badge/Designed%20by-ARGON%204%20(Gemini%204)-8A2BE2.svg)](#-authorship--origin)

> **A Linux filesystem "Black Box" flight recorder operating like an oscilloscope trigger:** keeps an in-memory circular buffer of file system mutations without touching the disk, detects rapid entropy spikes (ransomware attacks, runaway scripts, malicious deletions), and **instantly executes an atomic CoW snapshot (Btrfs / ZFS / Snapper)** while dumping forensic evidence for incident investigation.

---

## 📌 Why Does This Exist?

| Tool | Operating Principle | Drawbacks |
|---|---|---|
| **`auditd` / Syslog** | Synchronously logs syscalls to disk | High I/O overhead, disk space exhaustion, IOPS degradation |
| **`snapper` / Scheduled Backups** | Periodic snapshots on a blind timer (e.g. hourly) | Ransomware encrypts an entire drive in seconds; by the next hourly snapshot, data is already lost |
| **`fs-flight-recorder`** | **RAM Ring Buffer + Sliding Mutation Trigger** | **Zero disk writes** during routine operations. CoW snapshot executes in the exact millisecond an anomaly is recognized |

---

## 🏗 Architecture

```
                       Linux Kernel (VFS)
                                │
                        [ inotify events ]
                                │
                                ▼
             ┌──────────────────────────────────────┐
             │         fs-flight-recorder           │
             │                                      │
             │   ┌──────────────────────────────┐   │
             │   │    RAM Ring Buffer (N ev)    │   │
             │   │  [ev] [ev] [ev] [ev] [ev]... │   │
             │   └──────────────┬───────────────┘   │
             │                  │                   │
             │     Sliding Window Mutation Rate     │
             └──────────────────┬───────────────────┘
                                │
                 [ Anomaly Threshold Exceeded? ]
                     ├─── NO ───► Continue monitoring (Zero disk I/O)
                     │
                     └─── YES ──► 🚨 EMERGENCY TRIGGER
                                    │
                                    ├── 1. Instant CoW Snapshot (Btrfs / ZFS)
                                    └── 2. Dump Pre-Incident Ring Buffer to Disk
```

1. **In-Memory Ring Buffer**: Pre-allocates storage in RAM for $N$ recent events (default: 5000). Old events are silently overwritten in a circular fashion with zero disk wear.
2. **Sliding Time Window**: On each event batch, evaluates the total number of destructive operations (`CREATE`, `DELETE`, `MODIFY`, `MOVED`, `ATTRIB`) occurring within the last $M$ milliseconds (default: 50 mutations in 1000 ms).
3. **Reactive Defensive Trigger**: Once the threshold is exceeded, an emergency mitigation script runs immediately (read-only CoW filesystem snapshot, process quarantine, or network cut), followed by saving the ring buffer to disk as forensic evidence.
4. **Trigger Cooldown**: A built-in debounce timer suppresses repeated triggers during continuous high-load incidents.

---

## ⚡ Quick Start

### Prerequisites
* C++20 compliant compiler (`GCC >= 11` or `Clang >= 13`)
* Linux Kernel $\ge 2.6.13$
* `make` (optional)

### Build
```bash
# Clone the repository
git clone https://github.com/your-username/fs-flight-recorder.git
cd fs-flight-recorder

# Build using Makefile
make

# Or compile directly
g++ -std=c++20 -O3 -Wall -Wextra fs-flight-recorder.cpp -o fs-flight-recorder
```

### Usage
```bash
./fs-flight-recorder <directory_to_watch> [defensive_action_command]
```

Defaults:
* `<directory_to_watch>` = `/tmp`
* `[defensive_action_command]` = `echo '[Snapshot Triggered]'`
* Ring buffer size: **5000 events**
* Trigger threshold: **50 mutations** within **1000 ms** (1 second)

---

## 🛡 Defensive Integrations

### 1. Btrfs Read-Only CoW Snapshot
Instantly capture an immutable read-only snapshot upon attack detection:
```bash
./fs-flight-recorder /home \
  "btrfs subvolume snapshot -r /home /home/.snapshots/emergency_\$(date +%s)"
```

### 2. ZFS Atomic Snapshot
```bash
./fs-flight-recorder /tank/userdata \
  "zfs snapshot tank/userdata@emergency_\$(date +%s)"
```

### 3. Snapper Integration
```bash
./fs-flight-recorder /home \
  "snapper -c home create --description 'Emergency Anomaly Trigger' --cleanup-algorithm number"
```

### 4. Process Freezing via cgroups v2
Instantly freeze user processes to prevent further data encryption:
```bash
./fs-flight-recorder /home \
  "echo '1' > /sys/fs/cgroup/user.slice/cgroup.freeze && notify-send -u critical 'FS Alarm' 'Anomaly detected: user processes frozen!'"
```

---

## 🧪 Simulation and Testing

Start the flight recorder in one terminal:
```bash
mkdir -p /tmp/test_watch
./fs-flight-recorder /tmp/test_watch "echo '>>> EMERGENCY DEFENSIVE ACTION TRIGGERED! <<<'"
```

In another terminal, simulate a burst attack (create 60 files within milliseconds):
```bash
for i in {1..60}; do touch /tmp/test_watch/payload_$i; done
```

The daemon detects the rate surge and triggers the action:
```text
[*] Flight recorder started. Monitoring: /tmp/test_watch
[*] Anomaly threshold: 50 mutations within 1000 ms.

[!] WARNING: Anomaly detected! (50 operations within 1000 ms)
[!] Executing defensive action: echo '>>> EMERGENCY DEFENSIVE ACTION TRIGGERED! <<<'
>>> EMERGENCY DEFENSIVE ACTION TRIGGERED! <<<
[+] Flight recorder dump saved to: /tmp/flight_recorder_dump_1791970298.log
```

The saved forensic dump (`/tmp/flight_recorder_dump_*.log`) retains the exact timeline leading up to the trigger:
```text
=== FS FLIGHT RECORDER EMERGENCY DUMP ===
Total recorded events in ring: 50

2026-10-14 14:06:49 | Mask: [ CREATE ]  | File: /tmp/test_watch/payload_1
2026-10-14 14:06:49 | Mask: [ CHMOD/CHOWN ]  | File: /tmp/test_watch/payload_1
2026-10-14 14:06:49 | Mask: [ CREATE ]  | File: /tmp/test_watch/payload_2
...
```

Run automated tests:
```bash
make test
```

---

## ⚖️ Effectiveness and Architectural Analysis

The oscilloscope-style flight recorder concept introduces distinct advantages over classical security monitors, while presenting specific engineering trade-offs.

### Key Strengths
1. **Zero Steady-State Disk Footprint**: Disks and SSDs experience zero wear or throughput penalty under normal operation.
2. **"Patient Zero" Root-Cause Forensics**: Unlike logs collected after system failure, the ring buffer stores the exact precursor events right before the trigger fired.
3. **Sub-second Recovery Potential**: CoW snapshots taken at trigger onset preserve the vast majority of untouched files without waiting for scheduled backup windows.

### Technical Considerations and Solutions

| Consideration | Problem Mechanics | Mitigation / Architecture Evolution |
|---|---|---|
| **Non-recursive inotify** | `inotify_add_watch` tracks only the registered directory, not subfolders. | Maintain a recursive tree of watches, or upgrade to **`fanotify` / `eBPF`**. |
| **Missing Process Attribution (PID)** | Standard `inotify` reports mutated paths, but cannot identify the offending PID. | Transition to **`fanotify`** (`FAN_REPORT_PID`) or eBPF (`bpf_get_current_pid_tgid()`). |
| **Post-Factum Reaction** | Trigger fires after $N$ events occur; initial files are modified before mitigation starts. | Use synchronous permission hooks (`fanotify` `FAN_OPEN_PERM`) to inspect before file access. |
| **Synchronous Execution Delay** | `std::system()` blocks the `poll()` loop during snapshot creation, risking `IN_Q_OVERFLOW`. | Offload defensive commands and file dumping to a dedicated **background worker thread**. |
| **Heap Allocations in Fast-Path** | `std::string` concatenation triggers heap activity when paths exceed Small String Optimization. | Pre-allocate fixed-length buffers (`char[PATH_MAX]`) or dedicated memory arenas. |
| **False Positives** | Heavy developmental activity (`npm install`, `git checkout`, `cargo build`) triggers alarms. | Implement path ignore-lists (`node_modules`, `.git`, `target`) and Shannon entropy rate checks. |

---

## 🗺 Interception Technologies Compared

| Capability | inotify (Current) | fanotify | eBPF (LSM / Tracepoints) | auditd |
|---|---|---|---|---|
| **Disk I/O During Idle** | **Zero (in RAM)** | **Zero (in RAM)** | **Zero (in RAM)** | Heavy (Disk logging) |
| **Filesystem-Wide Coverage** | Requires per-folder watches | Entire mount point | Kernel-wide | Configured rules |
| **Process Attribution (PID/UID)** | None | `FAN_REPORT_PID` | Full metadata (PID, cgroup) | Detailed audit records |
| **Synchronous Access Prevention** | None (reactive only) | `FAN_OPEN_PERM` | `bpf_send_signal`, `-EPERM` | None (audit only) |
| **Pre-Write Entropy Inspection** | None | Intercept file descriptor | In-kernel buffer analysis | None |
| **Privileges Required** | Standard user | `CAP_SYS_ADMIN` (root) | `CAP_BPF` + `CAP_SYS_ADMIN` | root |

---

## 🤖 Authorship & Origin

The concept and core implementation of **`fs-flight-recorder`** were developed and designed by **ARGON 4 (GEMINI 4)**.

The utility was conceived to address a key gap in standard Linux distributions: an ultra-lightweight, in-memory protective watchdog combining an oscilloscope-style memory buffer with instantaneous CoW snapshot triggers to combat ransomware and runaway scripts.

---

## 📄 License

Distributed under the [MIT License](LICENSE).
