# ESP32 OTA Update Demonstration

A basic proof-of-concept for Over-The-Air (OTA) firmware updates on ESP32 devices.

## About The Project

This repository provides a working example of how to implement remote firmware updates for ESP32 microcontrollers. It demonstrates a core pipeline where devices can check for, download, and install updates over a Wi-Fi connection using a standard JSON manifest file.

This project is intended as a demonstration and a foundational template for prototyping remote update flows in maker projects.

## Core Features

* **Dynamic Hardware Detection:** Devices check their physical flash size (e.g., 4MB vs. 8MB) at runtime to request the appropriate firmware variant from the server.
* **Direct File Delivery:** Fetches firmware binaries directly from a hosted raw URL using cache-busting techniques to ensure the newest file is always downloaded.
* **Version Control Logic:** Includes basic logic to ensure devices only initiate a download when a newer version is available in the manifest.
* **Captive Portal Dashboard:** A lightweight web interface hosted on the ESP32 to view the current firmware version and trigger manual update checks.

## Getting Started

To get a local copy up and running, follow these steps.

### Prerequisites

* A C++ development environment configured for the ESP32 (such as the Arduino Core for ESP32).
* The [ESP32OTAPull Library](https://www.google.com/search?q=https://github.com/mottramlabs/ESP32OTAPull) installed in your environment.

### Initial Setup

1. Clone this repository to your local machine.
2. Open `main.cpp` and configure your Wi-Fi credentials or ensure your captive portal logic is active.
3. Define the initial version of your firmware at the top of your code:
```cpp
#define VERSION "1.0.0"

```


4. Flash the initial code to your ESP32 via a physical USB cable.

## The Update Workflow

Once the initial firmware is flashed via USB, you can use the OTA pipeline to push future updates wirelessly:

1. **Compile Binary:** Update your code (e.g., change `#define VERSION` to `"1.0.1"`), compile it, and locate the generated `.bin` file.
2. **Host the Files:** Upload your new `.bin` file to your server or a raw file hosting service (like raw GitHub user content).
3. **Update Manifest:** Edit the `manifest.json` file on your server to reflect the newest version number and the exact URL of the new `.bin` file. Ensure there are no invisible whitespace characters or line breaks in your JSON file.
```json
{"Configurations":[{"Board":"ESP32_DEV","Config":"4MB","Version":"1.0.1","URL":"<URL_TO_YOUR_RAW_BIN_FILE>"}]}

```


4. **Device Pull:** The ESP32 reads the JSON manifest. If the server's version number is higher than the device's current version, the ESP32 automatically downloads the binary file, writes it to its flash memory, and reboots.

## Status & Error Codes

When the device checks for an update, the OTA process returns a specific status code. You can use these codes to troubleshoot issues or trigger specific behaviors in your application.

| Code | Constant | Description |
| --- | --- | --- |
| `0` | `UPDATE_OK` | The update was successfully downloaded and written to flash memory. |
| `-1` | `NO_UPDATE_AVAILABLE` | A matching profile was found in the manifest, but the device is already running this version (or newer). |
| `-2` | `NO_UPDATE_PROFILE_FOUND` | The manifest was downloaded, but no configuration matched the device's specific `Board`, `Config`, or MAC address. |
| `-3` | `UPDATE_AVAILABLE` | A new update is available on the server, but the device was explicitly instructed to only check, not download. |
| `1` | `HTTP_FAILED` | Failed to connect to the server or download the file (e.g., URL is incorrect or server is down). |
| `2` | `WRITE_ERROR` | The file downloaded, but the device failed to write it to its internal flash memory. |
| `3` | `JSON_PROBLEM` | The manifest file could not be parsed. This is usually caused by invalid JSON formatting, invisible whitespace characters, or trailing commas. |
| `4` | `OTA_UPDATE_FAIL` | A partition error occurred. Ensure your ESP32 is using a partition scheme that supports OTA (e.g., "Default 4MB with spiffs"). |

## Acknowledgments

* [ESP32OTAPull Library](https://www.google.com/search?q=https://github.com/mottramlabs/ESP32OTAPull) - The core library handling the JSON parsing and binary download process.



```

```
