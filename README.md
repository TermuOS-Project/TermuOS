<p align="center">
  <img src="assets/logo.png" alt="TermuOS Logo" width="180">
</p>

<h1 align="center">TermuOS</h1>

<p align="center">
  A lightweight, open-source x86_64 operating system built for learning kernel development and low-level kernel programming.
</p>

<p align="center">
  <a href="https://github.com/TermuOS-Project/TermuOS">
    <img src="https://img.shields.io/github/stars/TermuOS-Project/TermuOS?style=for-the-badge" alt="GitHub Stars">
  </a>
  <a href="https://github.com/TermuOS-Project/TermuOS/forks">
    <img src="https://img.shields.io/github/forks/TermuOS-Project/TermuOS?style=for-the-badge" alt="GitHub Forks">
  </a>
  <a href="https://github.com/TermuOS-Project/TermuOS/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/TermuOS-Project/TermuOS?style=for-the-badge" alt="License">
  </a>
  <a href="https://github.com/TermuOS-Project/TermuOS/commits/main">
    <img src="https://img.shields.io/github/last-commit/TermuOS-Project/TermuOS?style=for-the-badge" alt="Last Commit">
  </a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Architecture-x86__64-2563eb?style=flat-square">
  <img src="https://img.shields.io/badge/Language-C%20%7C%20%20C++%7C%20Assembly-16a34a?style=flat-square">
  <img src="https://img.shields.io/badge/Build-Make-f59e0b?style=flat-square">
  <img src="https://img.shields.io/badge/Boot-Limine-8b5cf6?style=flat-square">
</p>

---

## ✨ Features

- 🖥️ x86_64 Kernel
- ⚙️ Multitasking
- 📁 Virtual File System (VFS)
- 💻 Terminal & Shell
- 🧠 Memory Management
- 🔌 Driver Support
- 🧩 Modular Architecture
- 🚀 Runs in QEMU

---

## 🚀 Getting Started

### Requirements

```bash
sudo apt install clang nasm make xorriso qemu-system-x86
```

### Clone

```bash
git clone https://github.com/TermuOS-Project/TermuOS.git
cd TermuOS
```

### Build

```bash
make
```

### Run

```bash
make run
```

---

## 📂 Project Structure

```text
kernel/      Kernel
arch/        Architecture-specific code
drivers/     Hardware drivers
fs/          File systems
mm/          Memory management
net/         Networking
shell/       Terminal & Shell
lib/         Libraries
build/       Build output
```

---

## 🗺️ Roadmap

- User-mode programs
- ELF executable loader
- Improved networking
- Better filesystem support
- SMP support
- GUI
- Audio
- Package manager

---

## 🤝 Contributing

Contributions, bug reports, and feature suggestions are always welcome!

---

## 📜 License

This project is licensed under the **MIT License**.

---

<p align="center">
  <b>Made with ❤️ by TermuOS Team</b>
  <b>Since 2022</b>
  <br><br>
  <a href="https://github.com/TermuOS-Project/TermuOS">GitHub</a> •
  <a href="https://termuos.netlify.app">Website</a>
</p>
