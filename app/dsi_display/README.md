# MIPI DSI Display Test Tool (`dsi_display`)

This utility allows testing the ESP32-P4 MIPI DSI display controller and Framebuffer (`/dev/fb0`).

## Usage

```bash
# 1. Query display resolution and framebuffer info
nsh> dsi_display info

# 2. Draw standard 8-color vertical test pattern bars
nsh> dsi_display bars

# 3. Fill screen with specific RGB565 / RGB888 color (R G B in 0..255)
nsh> dsi_display color 255 0 0     # Pure Red
nsh> dsi_display color 0 255 0     # Pure Green
nsh> dsi_display color 0 0 255     # Pure Blue

# 4. Draw geometric grid and border test pattern
nsh> dsi_display grid

# 5. Measure rendering frame rate benchmark for 5 seconds
nsh> dsi_display fps 5
```
