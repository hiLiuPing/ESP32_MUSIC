# ESP32-S3 Music Player

FreeRTOS-based local MP3 player for an ESP32-S3, ST7305 384x168 display and
WM8978 codec. The firmware is split into four layers:

- `src/bsp`: pins and hardware drivers for display, SD, codec and input.
- `src/app`: command parser, MP3 library and shared player state.
- `src/task`: FreeRTOS object creation and hardware/input/player/GUI tasks.
- `src/gui`: boot, home, music, read and setting pages.

The player does not connect to Wi-Fi and does not start playback at boot. It
recursively indexes up to 128 MP3 files from the SD card and waits on the first
track. The read and setting pages are intentional placeholders.

## Serial control

Use `115200 8N1` and terminate every command with CR or LF.

```text
page home|music|read|setting
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
```

On the music page, `up` and `down` select a track and `ok` starts it. Pressing
`ok` on the active track toggles pause. `prev` and `next` change the playing
track; `page-prev` and `page-next` change pages.

## Build

```powershell
pio run
```

The physical-key polling hook is `bsp_input_poll_key()`. It intentionally has
no GPIO mapping until the key pins and active levels are known.
