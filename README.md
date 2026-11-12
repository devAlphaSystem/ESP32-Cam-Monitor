# ESP32 Camera Watchdog

Camera Watchdog runs on an ESP32 connected to your local network and watches IP cameras that support ONVIF. It checks the camera every few seconds, sends an email when the camera fails, and restarts the camera by itself when it hangs. The ESP32 itself serves a web dashboard with the camera state, live video, and settings.

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

   | Field                                              | What it is for                                                                                                                            |
   | -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
   | `WIFI_SSID` and `WIFI_PASSWORD`                    | Wi-Fi name and password. You can leave them empty, because you can set up the Wi-Fi from your phone later.                                |
   | `DASHBOARD_ADMIN_PASSWORD_VALUE`                   | Password the dashboard asks for before saving settings and running actions. We recommend setting one.                                     |
   | `RESEND_API_KEY`, `RESEND_FROM`, and `ALERT_EMAIL` | Resend key, sender, and recipient of the alerts. The key starts with `re_`. Without it, the dashboard works and only the emails stay off. |

4. Connect the ESP32 by USB and upload the firmware.

   ```
   pio run -t upload
   ```

   In Visual Studio Code, the same command is the Upload button in the PlatformIO bottom bar. If the upload fails, hold the BOOT button on the board while PlatformIO connects, and release it when the upload starts.

5. Open the serial monitor to see the dashboard address.

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

The saved network stays in the ESP32 memory and survives restarts and power loss. It takes priority over the `secrets.h` network, which stays as a fallback. With the ESP32 already connected, the same page opens at `http://camera-watchdog.local/wifi`, or under Settings, Device, Choose another Wi-Fi network.

To protect the setup network with a password, set `WIFI_SETUP_AP_PASSWORD` in `include/app_config.h` to 8 to 63 characters. Without a password, phones show the notification to sign in to the network automatically, and the dashboard admin password is still required to save.

## Setting up the dashboard

Open `http://camera-watchdog.local/`, or the IP shown in the serial monitor. The Overview tab shows every registered camera with its latest image. If you see none, wait for discovery, which runs every 30 seconds by default, or open Settings, Actions, and use Rediscover cameras.

If the camera does not appear, check that it and the ESP32 are on the same subnet and that ONVIF is turned on in the camera app.

The sidebar lists the registered cameras with their state. Select a camera there to open it, or to switch the camera that the other tabs show.

The dashboard tabs.

| Tab       | What it shows                                                                                                                                                         |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Overview  | How many cameras are registered, online, offline, and recovering, and a card for each camera with a JPEG image loaded when you open the tab.                          |
| Camera    | Live H.264 video or JPEG snapshots of the selected camera, the PTZ presets, the ONVIF, RTSP, and image results, and a collapsed section with development diagnostics. |
| Resources | Memory, flash, and Wi-Fi signal of the ESP32.                                                                                                                         |
| Settings  | All the settings, the admin actions, the diagnostics, and the logs, in sections.                                                                                      |

The Settings sections.

- Device. Local address, Wi-Fi selection, dashboard refresh, and the status LED. Discovery and subnet scan options are in a closed group.
- Camera. Name and credentials for the selected camera. Snapshot timing, JPEG profile selection, RTSP port, and connection timeouts are in closed groups. These credentials do not change the camera account, and the saved password never returns to the browser.
- Monitoring. Check interval, failure and recovery thresholds, and email alerts. Startup grace and the rediscovery threshold are in a closed group.
- Recovery. Automatic ONVIF restart, grace after restart, cooldown, counting window, and maximum restarts. All restart limits are visible.
- Actions. Manual operations grouped by selected camera, all cameras, and device maintenance. Removing registrations, resetting the restart limiter, clearing logs, and erasing data ask for confirmation.
- Diagnostics. The full state as JSON, with a copy button.
- Logs. The persistent history, with filters and CSV export.

Save changes applies to all settings of the displayed scope, either the device or the selected camera. Fields are locked while a save or restore is in progress. Advanced groups open automatically when a field needs correction. Restore device defaults also resets the local hostname. Restore camera defaults keeps the camera username and password.

Switching browser tabs keeps unsaved edits. Opening Wi-Fi setup asks before discarding them and waits for any pending operation to finish.

The boot Wi-Fi wait, email retry interval, LED blink interval, and the two snapshot URI retry parameters have no controls in the dashboard. Normal saves keep their stored values, and restoring defaults resets them with the other settings in their scope.

To start, follow this order.

1. Under Settings, Camera, enter the camera username and password, if needed.
2. Under Settings, Monitoring, turn on the email alerts.
3. Under Settings, Actions, run Check now and check the Camera tab.

The default values are a check every 15 seconds, a grace period of 90 seconds after power-on, three failures to declare the camera offline, and at most three restarts per hour, with 5 minutes between them.

### Live video

The Camera tab opens on Live, which negotiates the H.264 stream over RTSP. Standard uses interleaved TCP. High resolution first tries RTP over UDP and uses TCP when UDP cannot be negotiated, when no RTP arrives, or after repeated relay overflows. That choice stays with the current camera and quality until the target changes, and its reason appears in the live diagnostics. The ESP32 relays the packets to the browser on port 81 without decoding anything, and the browser plays the video. Snapshot, next to Live, shows JPEG images instead.

- The Standard quality uses the smallest H.264 profile of the camera with at least 640 by 360 pixels. The High resolution quality uses the largest profile. Its bitrate and keyframe size must fit the capacity of the camera, Wi-Fi, and ESP32; a large keyframe can still delay the video.
- The ESP32 opens one session at a time, so two viewers can only watch the same camera at the same quality.
- Cameras that send only H.265 do not work on Live. Use Snapshot for them.
- The video stops by itself when you switch to Snapshot or leave the Camera tab, and the camera is free again.
- Development diagnostics on the Camera tab shows the current live JSON without stopping the video. It includes the negotiated transport and reason, fractional receive FPS over the last five rate samples, queued relay bytes, largest received frame, current RTP cumulative loss, and resync events. RTP cumulative loss follows RTCP semantics and can be negative when duplicate packets arrive; resync events accumulate since boot. The buffer shown by the player is available playback time, not capture-to-screen delay.
- The player keeps a playback reserve based on recent frame gaps, capped at 0.75 seconds. Catch-up preserves that reserve so a burst of frames does not empty the buffer before the next keyframe. Playback still resumes as soon as data is available, without a forced pause timer.
- The JSON also reports viewer socket write waits, send call duration, minimum free relay stack in bytes, actual Wi-Fi power-save mode, and maximum TX power. Write-block count, total blocked time, maximum write wait, and maximum send call time accumulate since boot. Waits measure elapsed wall time until write progress or closure, including any relay work between retries; they are not TCP RTT measurements.
- When viewer writes free relay space, the relay retries a buffered RTP packet before waiting for socket readiness. This avoids an idle wait while a complete packet can already be forwarded.

### Security

The dashboard uses HTTP, without encryption, and is meant for the local network. Do not open port 80 or 81 on the router to the internet. Set `DASHBOARD_ADMIN_PASSWORD_VALUE` so that no one on your network can change settings or restart cameras.

## Support firmwares

The `firmwares/` folder has two firmwares that help you test the board and clear the memory. The installers are Windows scripts, and each folder has an `install.bat`.

| Folder    | What it does                                                                               |
| --------- | ------------------------------------------------------------------------------------------ |
| `minimal` | Erases the whole flash and uploads a test firmware, with a page that shows the board data. |
| `zeroed`  | Erases the whole flash and uploads nothing.                                                |

### Before using the installers

- Install PlatformIO Core. The installer looks for `platformio.exe` in `%USERPROFILE%\.platformio\penv\Scripts`, then in the PATH. To use another path, set the environment variable `ESP32_PLATFORMIO_EXE`.
- Create `include/secrets.h` as described above. The `minimal` firmware uses the Wi-Fi from that file.
- Find the COM port of the ESP32 in the Windows Device Manager. You may need to install the USB driver of the board, such as CP210x or CH340.

To run an installer, double-click the `install.bat` in the folder, enter the port, such as `COM3`, and confirm with Y. Answering N cancels without changing the board.

Both modes erase everything, including the main firmware, the settings, the saved Wi-Fi network, and the logs. Use them only when you really want to wipe the board.

### minimal

Builds and uploads a diagnostic firmware. It connects to the Wi-Fi from `secrets.h` and shows the page `http://esp32-recovery.local/` with the chip model, memory, flash size, last reset reason, uptime, IP address, signal, and MAC. The page refreshes every 3 seconds, and the blue LED blinks every half second. Use it to confirm that the board, the antenna, and the Wi-Fi work.

### zeroed

Erases the entire flash, including the bootloader, settings, NVS, and logs. The board stays without firmware until you upload another one. Use it to return a board to its factory state before reusing it.

## Troubleshooting

- The `.local` address does not open. Use the IP that the serial monitor shows. On Windows, the `.local` address depends on the mDNS service, which a VPN or firewall may block.
- The ESP32 does not connect to Wi-Fi. Confirm that the network is 2.4 GHz and that the password is right. After 2 minutes, the `Camera-Watchdog-XXXX` network opens so you can fix it.
- The camera is not found. See the section on setting up the dashboard, and check that the camera accepts ONVIF without IP blocking.
- The live video stutters. Switch the quality to Standard and close other tabs that are watching the camera.
- The upload fails. Replace the USB cable, check the port in `upload_port`, and hold the BOOT button during the connection.
- I forgot the admin password. Change `DASHBOARD_ADMIN_PASSWORD_VALUE` in `secrets.h` and upload the firmware again.

## License

The project uses the MIT license. The full text is in the `LICENSE` file.
