# Type D HTTP/Serial Command API

Control this device by HTTP or Serial using the following hex-based codes.

## HTTP Usage

- Endpoint: **`GET /cmd?c=XX[&param=VAL]`** (on port 8080)
    - Example: `/cmd?c=01` (next image)
    - Example: `/cmd?c=20&val=50` (set brightness to 50)

## Serial Usage

- Send `c=XX[&param=VAL]` by serial (with newline/CR).
    - Example: `c=03` (random image)
    - Example: `c=20&val=80` (set brightness 80)

## Command Table

| Hex | Command           | Description                                  | Params                  |
|-----|-------------------|----------------------------------------------|-------------------------|
| 01  | NEXT_IMAGE        | Show next image                              |                         |
| 02  | PREV_IMAGE        | Show previous image                          |                         |
| 03  | RANDOM_IMAGE      | Show random image                            |                         |
| 04  | DISPLAY_MODE      | Set mode: jpg=0, gif=1, random=2             | mode=0/1/2 or mode=jpg/gif |
| 05  | DISPLAY_IMAGE     | Show image (filename)                        | file=FILENAME           |
| 06  | DISPLAY_CLEAR     | Clear the display                            |                         |
| 20  | BRIGHTNESS_SET    | Set display brightness                       | val=5-100               |
| 30  | WIFI_RESTART      | Restart WiFi portal (captive portal)         |                         |
| 31  | WIFI_FORGET       | Forget WiFi network and settings             |                         |
| 40  | REBOOT            | Reboot the device                            |                         |
| 60  | DISPLAY_ON        | Turn display ON (LovyanGFX)                  |                         |
| 61  | DISPLAY_OFF       | Turn display OFF (LovyanGFX)                 |                         |

---

## Example Requests

- **HTTP:**  
  `GET http://<DEVICE_IP>:8080/cmd?c=20&val=70`

- **Serial:**  
  Type `c=05&file=/gif/test.gif` and press Enter.

---

## Notes

- All valid HTTP requests get `{"ok":1}` as JSON reply.
- Unknown or invalid commands log an error on Serial.
- You can extend the command set easily by adding new cases.

---
