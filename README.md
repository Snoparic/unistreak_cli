# UniStreak CLI Client v1.0.0

Клиент для работы с эмулятором сервера регистратора UniStreak ® ВНИИА им. Духова (2‑я ревизия).

```txt
.
├── cmake
│   ├── toolchain-linux-x86_64.cmake
│   ├── toolchain-linux-x86.cmake
│   ├── toolchain-win32-x86.cmake
│   └── toolchain-windows-x64.cmake
├── CMakeLists.txt
├── LICENSE_EN
├── LICENSE_RU
├── main.c
├── README.md
└── scripts
    └── build-all.sh
```

## Инструкция по сборке из исходников

### Требования
- CMake 3.20+
- Make
- Компилятор Clang или GCC
- MSYS2 или WSL с MinGW (Windows)

### Сборка

Для сборки клиента из исходного кода необходимо:

1. Установить инструменты: CMake (≥3.20), Make и Clang.
2. Получить исходный код библиотек:
   - [UniStreakLib 1.0.0](https://github.com/Snoparic/unistreak_lib/releases/tag/v1.0.0)
   - [cJSON 1.7.19](https://github.com/DaveGamble/cJSON/releases/tag/v1.7.19)
3. Разместить файлы библиотеки (`unistreak.h`, `unistreak.c`) и cJSON (`cJSON.h`, `cJSON.c`) в той же директории, где находится `main.c`.
4. Запустить скрипт сборки:

```bash
cd scripts
./build-all.sh

```