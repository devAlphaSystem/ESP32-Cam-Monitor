# ESP32 Camera Watchdog

Camera Watchdog runs on an ESP32 connected to your local network and watches IP cameras that support ONVIF. It checks the camera every few seconds, sends an email when the camera fails, and restarts the camera by itself when it hangs. A web dashboard served by the ESP32 itself shows everything.

## What the firmware does

- Finds ONVIF cameras on the network through WS-Discovery and stores the registry in the ESP32 memory.
- Tests ONVIF, RTSP, and the JPEG image capture of each camera, and classifies the state as online, unstable, recovering, or offline.
- Restarts the camera with the ONVIF SystemReboot command after confirmed failures. The minimum interval between restarts and the hourly limit are configurable.
- Sends email alerts through the Resend service when the camera goes down, comes back, or changes IP.
- Shows live H.264 video in the browser, with up to two viewers at the same time.
- Moves the camera to PTZ presets saved on the camera.
- Keeps a history of up to 7,500 warnings and errors in a dedicated partition. The history survives restarts and can be exported as CSV.
- Opens its own Wi-Fi network for setup when no Wi-Fi is configured or when it loses the connection for more than 2 minutes.
- Blinks the blue LED of the board, on GPIO 2, while the camera is not online.

## What you need

- An ESP32 board with 4 MB of flash, such as an ESP32 DevKit. It does not need PSRAM.
- A USB cable that carries data.
- A 2.4 GHz Wi-Fi network. The ESP32 does not use 5 GHz.
- An IP camera with ONVIF turned on, on the same network.
- PlatformIO on a computer running Windows, Linux, or macOS. To install it, use the PlatformIO IDE extension for Visual Studio Code or PlatformIO Core.

Email, NTP time, and dated messages need internet access. Monitoring, the dashboard, and the video work on the local network only.

## Installing the main firmware

1. Download the project from GitHub and open the folder in Visual Studio Code, or use a terminal inside the folder.

2. Copy `include/secrets.example.h` to `include/secrets.h`.

3. Fill in `include/secrets.h`.

   | Field                                                           | What it is for                                                                                                                              |
   | --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
   | `WIFI_SSID` and `WIFI_PASSWORD`                                 | Wi-Fi name and password. You can leave them empty, because you can set up the Wi-Fi from your phone later.                                  |
   | `CAMERA_ONVIF_USERNAME_VALUE` and `CAMERA_ONVIF_PASSWORD_VALUE` | ONVIF username and password of the camera, used as the default for new cameras. Leave them empty if the camera does not ask for a password. |
   | `DASHBOARD_ADMIN_PASSWORD_VALUE`                                | Password the dashboard asks for before saving settings and running actions. We recommend setting one.                                       |
   | `RESEND_API_KEY`, `RESEND_FROM`, and `ALERT_EMAIL`              | Resend key, sender, and recipient of the alerts. The key starts with `re_`. Without it, the dashboard works and only the emails stay off.   |

4. In `platformio.ini`, replace `upload_port = COM5` with the port of your ESP32, or delete the line so PlatformIO detects the port.

5. Connect the ESP32 by USB and upload the firmware.

   ```
   pio run -t upload
   ```

   In Visual Studio Code, the same command is the Upload button in the PlatformIO bottom bar. If the upload fails, hold the BOOT button on the board while PlatformIO connects, and release it when the upload starts.

6. Open the serial monitor to see the dashboard address.

   ```
   pio device monitor
   ```

   The ESP32 prints the IP and the address `http://camera-watchdog.local/` as soon as it connects to Wi-Fi.

To update the firmware later, run `pio run -t upload` again. The script `scripts/preserve_nvs_upload.py` stops the upload from erasing the saved settings.

## Setting up Wi-Fi from your phone

If `secrets.h` has no Wi-Fi, or if the network does not respond for 2 minutes, the ESP32 opens the network `Camera-Watchdog-XXXX`, without a password. The last four characters differ from board to board.

1. Connect your phone to that network. On most devices the setup page opens by itself. If it does not, go to `http://192.168.4.1/`.
2. Choose your network from the list, sorted by signal strength. For a hidden network, tap Enter the network name.
3. Type the password and tap Test and connect.
4. The ESP32 tests the password, the IP address, and the signal quality. It saves the network only if the test passes.
5. Switch your phone back to your normal Wi-Fi and open `http://camera-watchdog.local/`.

The saved network stays in the ESP32 memory and survives restarts and power loss. It takes priority over the `secrets.h` network, which stays as a fallback. With the ESP32 already connected, the same page opens at `http://camera-watchdog.local/wifi`, or under Settings, Global, Choose another Wi-Fi network.

To protect the setup network with a password, set `WIFI_SETUP_AP_PASSWORD` in `include/app_config.h` to 8 to 63 characters. Without a password, phones show the notification to sign in to the network automatically, and the dashboard admin password is still required to save.

## Setting up the dashboard

Open `http://camera-watchdog.local/`, or the IP shown in the serial monitor. The Overview tab lists all cameras. If you see none, wait for discovery, which runs every 30 seconds by default, or open Actions and use Rediscover cameras.

If the camera does not appear, check that it and the ESP32 are on the same subnet and that ONVIF is turned on in the camera app. As an alternative, enter the IP in `CAMERA_IP_HINT` in the file `include/app_config.h` and upload the firmware again.

The dashboard tabs.

| Tab       | What it shows                                                                            |
| --------- | ---------------------------------------------------------------------------------------- |
| Overview  | State of all cameras.                                                                    |
| Camera    | Results of the ONVIF, RTSP, and image tests of the selected camera.                      |
| Live      | H.264 video from the selected camera and the PTZ presets.                                |
| Preview   | JPEG images, used for diagnosis.                                                         |
| Actions   | Check now, restart the camera, rediscover cameras, clear logs, and erase the ESP32 data. |
| Resources | Memory, flash, and Wi-Fi signal of the ESP32.                                            |
| Settings  | All the settings, in sections.                                                           |

The Settings sections.

- Global. Local name of the ESP32, Wi-Fi and discovery times, and the link to switch Wi-Fi network.
- Identification. Camera name, preferred ONVIF profile for the image, and RTSP port.
- Camera authentication. Camera username and password, used by the watchdog. They do not change the account inside the camera. The saved password never goes back to the browser.
- Monitoring. Interval between checks and how many failures lead to each state.
- Recovery. Automatic ONVIF restart, wait time, minimum interval, and hourly limit.
- Network and notifications. Turns on the email alerts and the IP change notice.
- Communication. Connection and response timeouts.
- Diagnostics. The full state as JSON, with a copy button.
- Logs. The persistent history, with filters and CSV export.

To start, follow this order.

1. Under Settings, Camera authentication, enter the camera username and password, if it asks for them.
2. Under Settings, Network and notifications, turn on the email alerts.
3. Under Actions, run Check now and check the Camera tab.

The default values are a check every 15 seconds, a grace period of 90 seconds after power-on, three failures to declare the camera offline, and at most three restarts per hour, with 5 minutes between them.

### Live video

The Live tab uses the H.264 stream of the camera over RTSP. The ESP32 relays the packets to the browser on port 81 without decoding anything, and the browser plays the video.

- The Standard quality uses the smallest H.264 profile of the camera with at least 640 by 360 pixels. The High resolution quality uses the largest profile and may stutter, because the camera sends that stream in bursts.
- The ESP32 opens one session at a time, so two viewers can only watch the same camera at the same quality.
- Cameras that send only H.265 do not work on this tab.
- The video stops by itself when you leave the tab, and the camera is free again.

### Security

The dashboard uses HTTP, without encryption, and is meant for the local network. Do not open port 80 or 81 on the router to the internet. Set `DASHBOARD_ADMIN_PASSWORD_VALUE` so that no one on your network can change settings or restart cameras.

## Support firmwares

The `firmwares/` folder has three firmwares that help you test the board, clear the memory, and recover the history. The installers are Windows scripts, and each folder has an `install.bat`.

| Folder       | What it does                                                                               |
| ------------ | ------------------------------------------------------------------------------------------ |
| `minimal`    | Erases the whole flash and uploads a test firmware, with a page that shows the board data. |
| `zeroed`     | Erases the whole flash and uploads nothing.                                                |
| `log-reader` | Saves a copy of the logs, erases the rest, and uploads a log reader.                       |

### Before using the installers

- Install PlatformIO Core. The installer looks for `platformio.exe` in `%USERPROFILE%\.platformio\penv\Scripts`, then in the PATH. To use another path, set the environment variable `ESP32_PLATFORMIO_EXE`.
- Create `include/secrets.h` as described above. The `minimal` and `log-reader` firmwares use the Wi-Fi from that file.
- Find the COM port of the ESP32 in the Windows Device Manager. You may need to install the USB driver of the board, such as CP210x or CH340.

To run an installer, double-click the `install.bat` in the folder, enter the port, such as `COM3`, and confirm with Y. Answering N cancels without changing the board.

The `minimal` and `zeroed` modes erase everything, including the main firmware, the settings, the saved Wi-Fi network, and the logs. Use these two only when you really want to wipe the board. To recover the logs before wiping, use `log-reader`.

### minimal

Builds and uploads a diagnostic firmware. It connects to the Wi-Fi from `secrets.h` and shows the page `http://esp32-recovery.local/` with the chip model, memory, flash size, last reset reason, uptime, IP address, signal, and MAC. The page refreshes every 3 seconds, and the blue LED blinks every half second. Use it to confirm that the board, the antenna, and the Wi-Fi work.

### zeroed

Erases the entire flash, including the bootloader, settings, NVS, and logs. The board stays without firmware until you upload another one. Use it to return a board to its factory state before reusing it.

### log-reader

Recovers the log history of a watchdog without losing any record. The installer runs these steps.

1. Builds the reader.
2. Saves a full 960 KiB copy of the logs partition in the folder `firmwares/log-reader/backups/` and checks the size and the SHA-256.
3. Erases everything except the logs partition.
4. Checks that the logs partition is still identical to the backup.
5. Uploads the reader and checks the logs again.

The process stops before erasing anything if it cannot confirm the backup, the flash size, or the identity of the board. Git ignores the `backups` folder, and it can contain names and addresses from your network, so treat it as sensitive material.

After the installation, open the reader in one of these ways.

- Through the Wi-Fi from `secrets.h`, at `http://esp32-log-reader.local/`.
- Through the network `ESP32-LogReader-XXXXXX`, with the password `Logs-XXXXXX`, where the last six characters are the same in both names. The address is `http://192.168.4.1/`.

The site filters by level, category, event, result, Boot ID, and Context ID. It exports the valid records as CSV and JSONL, and exports the whole partition as RAW, even if the file system is corrupted. To go back to monitoring, upload the main firmware with `pio run -t upload` in the root folder.

## Troubleshooting

- The `.local` address does not open. Use the IP that the serial monitor shows. On Windows, the `.local` address depends on the mDNS service, which a VPN or firewall may block.
- The ESP32 does not connect to Wi-Fi. Confirm that the network is 2.4 GHz and that the password is right. After 2 minutes, the `Camera-Watchdog-XXXX` network opens so you can fix it.
- The camera is not found. See the section on setting up the dashboard, and check that the camera accepts ONVIF without IP blocking.
- The live video stutters. Switch the quality to Standard and close other tabs that are watching the camera.
- The upload fails. Replace the USB cable, check the port in `upload_port`, and hold the BOOT button during the connection.
- I forgot the admin password. Change `DASHBOARD_ADMIN_PASSWORD_VALUE` in `secrets.h` and upload the firmware again.

## License

The project uses the MIT license. The full text is in the `LICENSE` file.
