# ESP32-S3 Music Player

FreeRTOS-based local MP3 player for an ESP32-S3, ST7305 384x168 display and
WM8978 codec. The firmware is split into four layers:

- `src/bsp`: pins and hardware drivers for display, SD, codec and input.
- `src/app`: command parser, MP3 library and shared player state.
- `src/task`: FreeRTOS object creation and hardware/input/player/GUI tasks.
- `src/gui`: boot, home, music, weather, poetry and setting pages.

The player does not start playback at boot. It recursively indexes up to 1000 MP3
files below `/music` into a PSRAM-backed path cache and waits on the first track. Weather/time
synchronization is disabled by default; when enabled in Setting, the firmware
uses the saved Wi-Fi profile to query NTP and QWeather on the configured interval.

When no Wi-Fi profile exists, or when `WIFI CONFIG` is started from Setting, the
device creates the `DuduClock` access point at `192.168.1.1`. Submit the Wi-Fi,
city and optional administrative region in the configuration page, then the
device closes the AP and performs an immediate synchronization.

## Serial control

Use `115200 8N1` and terminate every command with CR or LF.

```text
page home|music|weather|poetry|setting
page-prev
page-next
up
down
ok
play
pause
toggle
prev
next
vol 0..21
vol+
vol-
rescan
status
home-demo auto
home-demo pause
home-demo next
home-demo prev
home-demo status
home-demo 1..27
```

`HOME_DEMO_ENABLE` in `include/app/home_demo.h` is enabled by default for Home
scene testing. It cycles through the 27 time/weather scenes every two seconds.
Manual scene selection pauses the cycle; `home-demo auto` resumes it. Set the
macro to `0` for normal RTC and network weather operation.

Physical keys use two navigation levels. After switching pages with left and
right, click the middle key to enter that page's controls. While inside a page,
hold the right key to leave its controls; hold it again to return through page
history until reaching Home. The same press is not forwarded to the page. Home
has no internal controls, so clicking the middle key there has no effect.
Playback continues after leaving the music page.

On the music page, left and right move across volume, playback mode, previous,
play/pause, next and playlist. The middle key activates the selected control.
Volume and playlist open full-screen subviews; hold the middle key to return to
the control row. Playback mode cycles through repeat-all, repeat-one and a
shuffle queue that supports moving backward through recently played entries.

## Build

```powershell
pio run
```

The physical-key polling hook is `bsp_input_poll_key()`. It intentionally has
no GPIO mapping until the key pins and active levels are known.
