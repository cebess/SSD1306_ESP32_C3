This project (SSD1306_ESP32_C3) is to demonstrate the capabilities of the SSD1306, 128 x 64 Dot Matrix, OLED/PLED Segment/Common Driver with Controller to 
the ESP32-C3.

## Wiring

The SSD1306 communicates over I2C. Connect it to the ESP32-C3 as follows:

| SSD1306 Pin | ESP32-C3 Pin | Notes                  |
|-------------|--------------|------------------------|
| VCC         | 3V3          | Power (3.3V)           |
| GND         | GND          | Ground                 |
| SCL         | GPIO9        | I2C clock              |
| SDA         | GPIO8        | I2C data               |

```
   ESP32-C3                     SSD1306 OLED
  +---------+                  +-------------+
  |     3V3 |----------------->| VCC         |
  |     GND |----------------->| GND         |
  |   GPIO9 |----------------->| SCL         |
  |   GPIO8 |----------------->| SDA         |
  +---------+                  +-------------+
```

I2C address used in code: `0x3C` (change to `0x3D` if your module uses that address).

Adjust `I2C_SDA` / `I2C_SCL` in [src/main.cpp](src/main.cpp) if your board wiring differs.
