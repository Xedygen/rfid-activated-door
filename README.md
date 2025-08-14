# ESP32 RFID Access Control System with Google Sheets Logging

This project creates a robust and flexible RFID access control system using an ESP32, two RFID readers, and Google Sheets as a backend for logging and user management.

## Features
- **Dual Reader Support:** Manages two MFRC522 RFID readers, perfect for separate entrance and exit points.
- **Google Sheets Integration:**
    - All access attempts (granted or denied) are logged to a Google Sheet with a timestamp.
    - Authorized card UIDs are managed in a separate sheet, allowing for easy remote updates.
- **Multiple Access Roles:**
    - **ADMIN:** Hardcoded UIDs in the firmware that always have access.
    - **GUEST:** UIDs learned and stored in the Google Sheet.
    - **MANUAL_OPEN:** A dedicated pushbutton to open the door without a card.
- **On-the-Fly UID Learning:** A dedicated "Learn Mode" allows for adding new user cards without reprogramming the device.
- **Asynchronous & Non-Blocking:** Uses the ESP32's dual cores to handle network requests without slowing down card reading, ensuring a responsive user experience.

---

## Hardware Requirements (Final Setup)
- 1 x ESP32 Development Board (DOIT DEVKIT V1 or similar)
- 2 x MFRC522 RFID Readers with cards/fobs
- 1 x 12V Single-Channel Relay Module (3-pin: VCC, GND, IN)
- 2 x Momentary Pushbuttons
- 1 x 12V DC Power Source (e.g., AC-to-DC transformer/adapter)
- 1 x LM2596 Buck (Step-Down) Converter (to create a 3.3V supply for the ESP32)
- 1 x 12V Electronic Door Lock
- Breadboard and Jumper Wires

---

## Pin Configuration & Wiring

### Logic and Sensor Wiring

| ESP32 Pin | Component                | Component Pin | Purpose                  |
| :-------- | :----------------------- | :------------ | :----------------------- |
| **GPIO 18** | RFID Reader 1 & 2        | SCK           | Shared SPI Clock         |
| **GPIO 19** | RFID Reader 1 & 2        | MISO          | Shared SPI Master In     |
| **GPIO 23** | RFID Reader 1 & 2        | MOSI          | Shared SPI Master Out    |
| **GPIO 2** | RFID Reader 1 (Entrance) | SDA (SS)      | Select for Reader 1      |
| **GPIO 22** | RFID Reader 1 (Entrance) | RST           | Reset for Reader 1       |
| **GPIO 4** | RFID Reader 2 (Exit)     | SDA (SS)      | Select for Reader 2      |
| **GPIO 21** | RFID Reader 2 (Exit)     | RST           | Reset for Reader 2       |
| **3V3** | RFID Reader 1 & 2        | 3.3V          | Power for RFID Readers   |
| **GND** | RFID Reader 1 & 2        | GND           | Ground for RFID Readers  |
| **GPIO 13** | Relay Module             | IN            | Relay Trigger Signal     |
| **GPIO 12** | Mode Button              | Leg 1         | Switch to Learn Mode     |
| **GND** | Mode Button              | Leg 2         | Ground for Mode Button   |
| **GPIO 27** | Door Open Button         | Leg 1         | Manual Door Open         |
| **GND** | Door Open Button         | Leg 2         | Ground for Door Button   |

### Power Wiring Diagram

1.  **12V Source -> LM2596 IN:** The 12V from the transformer powers the input of the buck converter.
2.  **LM2596 OUT (Calibrated to 3.3V) -> ESP32 3V3 & GND:** The converter's output provides clean 3.3V power directly to the ESP32.
3.  **12V Source -> Relay VCC & GND:** The 12V from the transformer also powers the relay module directly.
4.  **12V Source -> Relay COM & Door Lock:** The 12V from the transformer is switched by the relay to power the door lock.
5.  **Common Ground:** All ground connections (Transformer, LM2596, ESP32, Relay) must be tied together.

---

## Software & Script Setup

### 1. Arduino IDE Setup
1.  **Install ESP32 Boards:**
    - Go to `File > Preferences`.
    - Add this URL to "Additional boards manager URLs": `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
    - Go to `Tools > Board > Boards Manager...`, search for `esp32`, and install the package by Espressif Systems.
2.  **Select Board:** Go to `Tools > Board > esp32` and select **"DOIT ESP32 DEVKIT V1"**.
3.  **Install Libraries:** Go to `Sketch > Include Library > Manage Libraries...` and install `MFRC522` by GithubCommunity.
4.  **Configure Code:** Open the main C++ sketch and fill in your credentials:
    ```cpp
    const char* ssid = "YOUR_WIFI_NAME";
    const char* password = "YOUR_WIFI_PASSWORD";
    const char* googleScriptURL = "YOUR_GOOGLE_SCRIPT_URL";
    const char* googleUIDSheetCSV = "YOUR_PUBLISHED_UID_SHEET_CSV_URL";
    ```

### 2. Google Sheets & Apps Script Setup
1.  **Create Sheet:** Create a new Google Sheet.
2.  **Create Tabs:** Rename the default sheet to **`UIDs`**. Create a new sheet and name it **`Logs`**. The names must be exact.
3.  **Open Apps Script:** Go to `Extensions > Apps Script`.
4.  **Paste Script:** Paste the provided Google Apps Script code into the editor. The file can be named `Code.gs` or `Code.js`.
5.  **Deploy:**
    - Click **Deploy > New deployment**.
    - For "Select type," choose **Web app**.
    - Under "Who has access," select **Anyone**. This is critical.
    - Click **Deploy**. Authorize the script when prompted.
    - Copy the **Web app URL** and paste it into the `googleScriptURL` variable in your C++ code.
6.  **Publish UIDs Sheet:**
    - Go back to the Google Sheet.
    - Click `File > Share > Publish to web`.
    - Under "Link," select the **UIDs** sheet and choose **Comma-separated values (.csv)**.
    - Click **Publish**.
    - Copy the generated link and paste it into the `googleUIDSheetCSV` variable in your C++ code.

---

## How to Use the System

- **Normal Mode (Door Unlock):**
  - The system starts in this mode.
  - Scan an ADMIN or GUEST card to activate the relay for 5 seconds.
  - Press the "Door Open Button" (GPIO 27) to activate the relay at any time.
  - All attempts are logged to the "Logs" sheet.

- **Learn Mode:**
  - Press the "Mode Button" (GPIO 12) to enter Learn Mode.
  - Scan a new, non-admin card on either reader.
  - The card's UID will be automatically added to the "UIDs" sheet.
  - Press the "Mode Button" again to return to Normal Mode. The system will automatically fetch the updated UID list.

## Troubleshooting
- **ESP32 not connecting to WiFi:** Double-check your SSID/Password. Ensure your network is 2.4 GHz.
- **Code won't upload / COM port not found:** You need to install the **CP2102 driver** for your ESP32 board.
- **UIDs not saving:** The most common cause is the Google Apps Script not being deployed with "Who has access" set to **"Anyone"**. Re-deploy the script and verify the setting.
- **System reboots when relay clicks:** This is a power issue caused by voltage sag. Your current setup (sharing a 12V source between the relay and the ESP32's converter) can cause this. If it becomes a problem, the most robust solution is to use an **opto-isolated relay module** (with a `JD-VCC` jumper) and a separate 5V power supply for the relay coil, as discussed previously.

