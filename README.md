# nexigen-cpp ⚡

The backbone of the **nexigen** ecosystem — an ultra-high performance C++20 core library designed for hard real-time systems and hardware simulator platforms. Purpose-built for minimum latency execution under **Ubuntu 26.04 LTS** (and modern Linux environments).

## 🚀 Key Architectural Features
* **Real-Time Ready:** Strictly zero-lock (lock-free / wait-free primitives) implementation within critical execution data paths (hot paths).
* **Deep OS Integration:** Direct interaction with low-level Linux system calls (`epoll`, `io_uring`, POSIX signals, and high-resolution timers).
* **Zero-Cost Abstractions:** O3, native architecture deployment (`-march=native`), and Link-Time Optimization (LTO) for bare-metal execution speed.

## 🛠️ Core Components
* Asynchronous lock-free ring-buffer Logger.
* Atomic-based Metrics collection and high-resolution Tracer.
* Base `AppClass` framework handling OS signal intercepts and predictable application lifecycles.

## 📄 License
Distributed under the **MIT License**. Created by Vasily Kovrigin.
