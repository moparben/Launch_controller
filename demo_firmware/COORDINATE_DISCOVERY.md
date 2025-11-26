# GT911 Touch Coordinate Discovery Progress

## Critical Discovery: Display 180° Rotation Issue

### Orientation Test Results
- **RED square (coded as TOP-LEFT 0,0)** → User sees: BOTTOM-LEFT
- **BLUE square (coded as BOTTOM-LEFT 0,1280)** → User sees: TOP-LEFT
- **GREEN square (coded as TOP-RIGHT 800,0)** → User sees: BOTTOM-RIGHT  
- **YELLOW square (coded as BOTTOM-RIGHT 800,1280)** → User sees: TOP-RIGHT

**CONCLUSION: Both X and Y axes are inverted (180° rotation)**

### Required Fix
Need to invert both coordinates:
```c
// Coordinate inversion for 180° rotation
transformed_x = DISPLAY_H_RES - raw_x;  // Invert X: 800 - x
transformed_y = DISPLAY_V_RES - raw_y;  // Invert Y: 1280 - y
```

### Current Configuration
- Touch Controller: GT911
- Display: 10.1" Waveshare MIPI-DSI (800x1280)
- ESP-IDF: v5.4.2
- No coordinate swapping (.swap_xy = 0)
- No mirroring (.mirror_x = 0, .mirror_y = 0)
- Raw coordinates need 180° inversion

### Next Steps
1. Implement coordinate inversion in touch_adjust_coordinates()
2. Test button detection with corrected coordinates
3. Validate calibration system functionality

## Technical Details
- Display Resolution: 800x1280 (DISPLAY_H_RES x DISPLAY_V_RES)
- Touch Range: X=0-800, Y=0-1280
- GPIO Configuration: RST=GPIO27, INT=NC (official BSP)
- I2C: GPIO7=SDA, GPIO8=SCL
- MIPI-DSI Timing: 960 Mbps lane rate (optimized)

## Official BSP Comparison
Waveshare official BSP uses:
- .x_max = BSP_LCD_H_RES (800)
- .y_max = BSP_LCD_V_RES (1280) 
- .swap_xy = 0
- .mirror_x = 1
- .mirror_y = 1

Our discovery shows manual coordinate inversion needed instead of BSP mirroring flags.