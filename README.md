# fitkit3-snake

A bare-metal implementation of the classic **Snake** game in C for the **FITkit 3** development board (NXP Kinetis **MK60D10**, ARM Cortex-M4). The game runs on two 8x8 LED matrices (a 16x8 display) and is controlled with the on-board buttons.

> **Demo:** _add a GIF or a link to a video here_

## Features

- Snake rendered on a 16x8 LED matrix using time-multiplexed column/row selection
- Direction control with four buttons (up, down, left, right)
- Dedicated reset button that restarts the game
- Protection against reversing direction (the snake cannot turn 180 degrees into itself)
- Screen wrap-around: the snake leaves one edge and appears on the opposite one
- Button input handled through GPIO interrupts (PORTE IRQ, falling edge, internal pull-ups)

## Hardware and tools

| Item | Details |
|------|---------|
| Board | FITkit 3 |
| MCU | NXP Kinetis MK60D10 (ARM Cortex-M4) |
| Display | 2x 8x8 LED matrix, driven directly via GPIO (PORTA) |
| Input | 5 push buttons on PORTE (pins 10, 11, 12, 26, 27) |
| IDE | Kinetis Design Studio (KDS) |
| Language | C (GNU11) |

## How it works

The LED matrix can only light one LED position at a time per column/row selection, so the display is **multiplexed**: the program cycles through every segment of the snake, selects its column and row, and holds it for a short delay. Done fast enough, all segments appear lit at once.

Display wiring is described by two small lookup tables (`COL_ADDR_PINS` for the 4-bit column decoder, `ROW_PINS` for the 8 row lines), so selecting a column or row is a single masked write to the GPIO output register.

| Function | Responsibility |
|----------|----------------|
| `SystemConfig()` | Enables port clocks, configures display pins as outputs, sets up the buttons and the PORTE interrupt |
| `configureButton()` | Configures one pin as a button: pull-up, interrupt on falling edge |
| `selectColumn()` | Writes a column number (0-15) to the 4 address lines of the column decoder |
| `selectRow()` | Activates exactly one of the 8 row lines |
| `snakeOutput()` | Draws every snake segment by multiplexing columns and rows |
| `moveSnake()` | Moves each segment one step in its stored direction, with wrap-around |
| `changeTailSnake()` | Propagates the head direction down the body, so segments follow the head |
| `turnSnake()` | Changes the head direction, rejecting opposite-direction turns |
| `PORTE_IRQHandler()` | Reads and clears the interrupt flags and dispatches button presses |
| `resetSnake()` | Restores the initial snake position, length and direction |

The main loop redraws the display many times per step, then moves the snake and waits before the next step, which sets the game speed. State shared between the main loop and the interrupt handler is declared `volatile`.

Game parameters (snake length, display size, timing, pin assignments) are defined as constants at the top of `main.c`.

## Project structure

```
.
├── main.c      # the whole game
└── README.md
```

Device and CMSIS headers (`MK60D10.h`, `system_MK60D10.h`, `core_cm4.h`, ...) are provided by the Kinetis SDK / KDS and are not part of this repository.

## Build and run

1. Install [Kinetis Design Studio](https://www.nxp.com/design/software/development-software/kinetis-design-studio-integrated-development-environment-ide:KDS_IDE).
2. Create a new bare-metal project for the **MK60DN512xxx10** and add `main.c`.
3. Connect the FITkit 3 over USB, then build and flash the project through the debugger.

## Controls

| Button | Action |
|--------|--------|
| Up / Down / Left / Right | Change the snake's direction |
| Reset | Restart the game |

## Limitations and possible improvements

This is a simplified version of the original game:

- Fixed snake length (4 segments), no food and no growth
- No collision detection or score
- Delays are implemented as busy-wait loops; a hardware timer (PIT) would give precise and CPU-friendly timing and a constant frame rate
- No software debouncing of the buttons
- The reverse-direction check compares against the current head direction only, so two quick presses within one step can still produce a reversal

## Author

Jonas Herzig
