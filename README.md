# Wake Word → Camera Stream
### ESP32-S3-Eye | esp-idf v5.1.2

Say **"Hi ESP"** → camera powers on → live MJPEG stream in browser.

---

## Requirements

- ESP32-S3-Eye
- Compatible **data** cable (not charge-only) — use the **UART** port on the board
- ESP-IDF v5.1.2
- esp-skainet cloned alongside

---

## Dependencies

Install system packages first:

**Arch Linux:**
```bash
sudo pacman -S --needed git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja ccache libffi openssl dfu-util libusb
```

**Ubuntu/Debian:**
```bash
sudo apt-get install git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0
```

**Windows:** Use WSL2 with Ubuntu, then follow Ubuntu steps. For USB access install [usbipd-win](https://github.com/dorssel/usbipd-win):
```powershell
winget install usbipd
usbipd list                         # find your ESP32's bus ID
usbipd bind --busid <busid>
usbipd attach --wsl --busid <busid>
```

**macOS:**
```bash
brew install cmake ninja dfu-util python3
```
Use `/dev/cu.usbmodem*` instead of `/dev/ttyACM0` in all commands below.

---

## ESP-IDF Setup

```bash
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.1.2
git submodule update --init --recursive
./install.sh esp32s3
```

Add alias to activate environment:
```bash
echo 'alias get_idf=". ~/esp/esp-idf/export.sh"' >> ~/.bashrc
source ~/.bashrc
```

Run `get_idf` in any new terminal before building.

---

## esp-skainet Setup

```bash
cd ~/esp
git clone https://github.com/espressif/esp-skainet.git
```

---

## Project Setup

### 1. Set WiFi credentials
Open `main/main.c` and edit:
```c
#define WIFI_SSID      "your_network_name"
#define WIFI_PASSWORD  "your_password"
```

### 2. Activate IDF environment
```bash
get_idf
```

### 3. Set target
```bash
cd ~/esp/aegis_warden
idf.py set-target esp32s3
```

### 4. Verify board config
The sdkconfig must have ESP32-S3-Eye selected, not KORVO or BOX:
```bash
grep "BOARD" sdkconfig | grep "=y"
# Must show: CONFIG_ESP32_S3_EYE_BOARD=y
```

If it shows anything else:
```bash
sed -i 's/CONFIG_ESP32_S3_KORVO_1_V4_0_BOARD=y/# CONFIG_ESP32_S3_KORVO_1_V4_0_BOARD is not set/' sdkconfig
sed -i 's/# CONFIG_ESP32_S3_EYE_BOARD is not set/CONFIG_ESP32_S3_EYE_BOARD=y/' sdkconfig
```

### 5. Build
```bash
idf.py build
```
First build downloads components (~50MB) and takes 3–5 minutes.

### 6. Flash
```bash
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash monitor
```

> Always `erase-flash` before first flash — skipping this causes WiFi to fail with `nvs_open fail`.

---

## Usage

After flashing, the monitor will show:
```
=========================================
 WiFi ready! Say 'Hi ESP' to activate.
 Stream will appear at:
 http://192.168.x.x
=========================================
----------- detect start -----------
```

1. Say **"Hi ESP"** toward the board
2. Monitor confirms:
   ```
   >>> WAKE WORD DETECTED <<<
   Camera ACTIVE - open browser to stream
   ```
3. Open `http://192.168.x.x` in any browser on the same WiFi

---

## Troubleshooting

**Board not detected / port keeps disappearing**
The board is crash-looping. Force bootloader mode to flash:
1. Hold **BOOT** button
2. Plug in USB while holding BOOT
3. Run flash command immediately
4. Release BOOT once flashing starts

**`wifi osi_nvs_open fail ret=4353`**
NVS partition missing or corrupted. Run `erase-flash` before flashing.

**Flash size mismatch error on boot**
```bash
idf.py menuconfig
# Serial flasher config → Flash size → 8MB
idf.py build flash monitor
```

**Wrong board selected (crashes on boot)**
Follow step 4 above to fix the sdkconfig board selection.

**Port busy or permission denied**
```bash
sudo fuser -k /dev/ttyACM0          # kill whatever holds the port
sudo usermod -aG dialout $USER      # fix permissions (log out after)
```

**VS Code serial monitor holding the port**
Close any open terminal/monitor in VS Code before flashing.