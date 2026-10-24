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


У вас появилось 3 режима сборки для разных задач:
1. Боевая сборка для Real-Time (Максимальная скорость):bash
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
Используйте код с осторожностью.
2. Проверка lock-free очередей и атомиков на гонки (ThreadSanitizer):bash
cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON
cmake --build build/tsan
Используйте код с осторожностью.
3. Проверка утечек памяти и выхода за границы массивов (AddressSanitizer):bash
cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON
cmake --build build/asan
