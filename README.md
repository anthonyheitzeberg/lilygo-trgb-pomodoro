# 🍅 Pomodoro Timer — LILYGO T-RGB ESP32-S3

A minimal, well-architected Pomodoro timer running on the **LILYGO T-RGB 2.1" Round Display** (ESP32-S3R8). Built with ESP-IDF v6.0 as a hands-on embedded systems learning project.

---

## Hardware

| Component | Details |
|---|---|
| Board | LILYGO T-RGB 2.1" Round Display |
| MCU | ESP32-S3R8 (dual-core Xtensa LX7, 8 MB PSRAM) |
| Display | 480×480 ST7701S, RGB parallel interface |
| Flash | 16 MB QIO |

---

## Features

- **25-minute work timer** displayed in large 8-bit bitmap font
- **5-minute break timer** auto-starts when work session expires
- **Start / Pause** button — tap to start, tap again to pause mid-session
- **Reset** button — returns to idle from any state
- **Session counter** — tracks completed work+break cycles
- **Color-coded states** — red for work, green for break, orange for paused
- Renders at 200 ms polling rate, redraws only on state change (efficient)

---

## UI Layout

```
┌─────────────────────────────┐
│                             │
│           WORK              │  ← red when running
│          25:00              │
│   ───────────────────       │
│           BREAK             │  ← green when running
│          05:00              │
│                             │
│           IDLE          #0  │  ← status + session count
└─────────────────────────────┘
```

---

## Project Structure

```
pomodoro_timer/
├── CMakeLists.txt              # Top-level ESP-IDF build config
├── sdkconfig.defaults          # Reproducible Kconfig defaults (committed)
├── .gitignore
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                  # Entry point — wires subsystems together
│   ├── pomodoro.c / .h         # FSM + countdown logic (esp_timer)
│   ├── display.c / .h          # Framebuffer renderer + 8×8 bitmap font
│   └── input.c / .h            # Debounced GPIO button polling
└── components/
    └── lcd_driver/
        ├── st7701s.c / .h      # ST7701S init sequence over 3-wire SPI
        └── lcd_panel.c / .h    # RGB parallel panel + PSRAM framebuffer
```

### Architecture Principles

- **Single Responsibility** — each module does exactly one thing
- **HAL Pattern** — display logic is hardware-agnostic; only `lcd_panel.c` touches the peripheral
- **Clean FSM** — all Pomodoro logic lives in `pomodoro.c`, zero hardware dependencies
- **FreeRTOS tasks** — separate tasks for input polling and display rendering
- **Thread safety** — shared state protected by `portENTER_CRITICAL` spinlocks

---

## Pomodoro State Machine

```
IDLE ──[start]──► WORK_RUNNING ──[pause]──► WORK_PAUSED
                       │                         │
                    [expire]                  [start]
                       │                         │
                       ▼                         ▼
                 BREAK_RUNNING ◄────────── WORK_RUNNING
                       │
                    [expire]
                       │
                       ▼
                     IDLE  (sessions_completed++)

Any state ──[reset]──► IDLE
```

---

## Pin Map (LILYGO T-RGB v1.1)

> ⚠️ Always verify against your board revision's schematic at  
> https://github.com/Xinyuan-LilyGO/LilyGo-T-RGB

### SPI Command Bus (ST7701S init only)
| Signal | GPIO |
|---|---|
| CS | 39 |
| SCLK | 48 |
| MOSI | 47 |
| Reset | 40 |
| Backlight | 46 |

### RGB Parallel Bus (pixel data)
| Signal | GPIO |
|---|---|
| PCLK | 21 |
| VSYNC | 3 |
| HSYNC | 46 |
| DE | 5 |
| B[4:0] | 14, 38, 18, 17, 16 |
| G[5:0] | 15, 13, 12, 11, 10, 9 |
| R[4:0] | 8, 7, 6, 5, 4 |

### Buttons
| Button | GPIO | Function |
|---|---|---|
| Boot button | 0 | Start / Pause |
| Side button | 14 | Reset |

---

## Requirements

- [ESP-IDF v6.0](https://github.com/espressif/esp-idf) (or v5.5.x)
- [EIM — ESP-IDF Installation Manager](https://docs.espressif.com/projects/idf-im-ui/en/latest/)
- macOS / Linux / Windows
- LILYGO T-RGB 2.1" board

---

## Getting Started

### 1. Clone the repo

```bash
git clone https://github.com/yourname/pomodoro-timer-esp32.git
cd pomodoro-timer-esp32
```

### 2. Install ESP-IDF via EIM

```bash
brew tap espressif/eim
brew install eim
eim install        # installs latest stable IDF
```

### 3. Activate the environment

```bash
eim shell          # drops you into a shell with IDF env vars set
```

### 4. Set target and build

```bash
idf.py set-target esp32s3
idf.py build
```

### 5. Flash and monitor

Plug in your board via USB, then:

```bash
idf.py -p /dev/tty.usbmodem1101 flash monitor
```

Replace `/dev/tty.usbmodem1101` with your actual port. To find it:

```bash
ls /dev/tty.*        # before plugging in
ls /dev/tty.*        # after plugging in — the new entry is your board
```

**Exit the monitor:** `Ctrl+]`

### If flashing fails to connect

Hold **BOOT**, tap **RESET**, release **BOOT** — this forces the ESP32-S3 into download mode.

---

## VS Code Setup

1. Install the **ESP-IDF** extension by Espressif Systems
2. Open command palette (`Cmd+Shift+P`) → `ESP-IDF: Configure ESP-IDF Extension`
3. Choose **Use existing installation** and set path to:
   ```
   /Users/<you>/.espressif/v6.0/esp-idf
   ```
4. Command palette → `ESP-IDF: Add .vscode Configuration Folder`

This generates `.vscode/c_cpp_properties.json` with correct include paths and compiler settings, eliminating all IntelliSense red squiggles.

---

## Key Concepts Covered

| Concept | Where |
|---|---|
| ESP-IDF CMake build system | `CMakeLists.txt` files |
| SPI peripheral (init bus) | `st7701s.c` |
| RGB LCD DMA peripheral | `lcd_panel.c` |
| PSRAM framebuffer | `lcd_panel.c` |
| GPIO configuration | `input.c`, `st7701s.c` |
| `esp_timer` (high-res periodic) | `pomodoro.c` |
| FreeRTOS tasks & priorities | `input.c`, `main.c` |
| Cross-core spinlocks | `pomodoro.c` |
| Software button debouncing | `input.c` |
| Bitmap font rendering | `display.c` |
| Finite State Machine pattern | `pomodoro.h/c` |

---

## License

MIT