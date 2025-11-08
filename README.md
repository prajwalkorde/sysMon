# 🖥️ sysMon System Monitor (Like `htop`)

A simple **System Monitor** written in **C** for Linux (tested on Ubuntu).  
It displays **CPU usage**, **memory usage**, and a list of **running processes** — all by reading from the `/proc` filesystem, without using any external libraries like `ncurses`.

---

## 🚀 Features

- 📊 Real-time CPU usage monitoring (reads from `/proc/stat`)
- 🧠 Memory usage display (reads from `/proc/meminfo`)
- ⚙️ Shows running processes with:
  - Process ID (PID)
  - CPU usage percentage
  - Memory usage (in KB)
  - Command name
- 🔁 Automatically refreshes every 1 second
- 💡 Implemented using:
  - File I/O
  - String parsing
  - `/proc` filesystem traversal

---

## 🧩 Concepts Used

- **File I/O in C** (`fopen`, `fgets`, `fscanf`, etc.)
- **String manipulation & parsing**
- **Process iteration** via `/proc/[pid]/` directories
- **System resource monitoring**
- **Basic ANSI escape sequences** for terminal refresh

---

## 🛠️ Build and Run

### 1. Clone the Repository
```bash
git clone https://github.com/<your-username>/system-monitor.git
cd system-monitor
