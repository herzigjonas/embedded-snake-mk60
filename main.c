/**
 * FITkit 3 Snake
 *
 * Simple Snake game for the FITkit 3 board (NXP Kinetis MK60D10, Cortex-M4).
 * The snake is drawn on two 8x8 LED matrices (16 columns x 8 rows) using
 * time-multiplexing and is steered with the on-board buttons (PORTE interrupts).
 *
 * Author: Jonas Herzig
 */

#include "MK60D10.h"

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

#define PIN(n)              (1u << (n))

#define DISPLAY_COLS        16
#define DISPLAY_ROWS        8
#define SNAKE_LENGTH        4

/* Initial snake position (head is segment 0). */
#define START_X             8
#define START_Y             4

/* Timing (busy-wait loop iterations). */
#define SEGMENT_HOLD_OUTER  20      /* how long one segment stays lit        */
#define SEGMENT_HOLD_INNER  50
#define REFRESHES_PER_STEP  100     /* display refreshes between two moves   */
#define STEP_DELAY_OUTER    1000    /* pause after each move (game speed)    */
#define STEP_DELAY_INNER    100

/* Directions. UP/DOWN move along the 16-column axis, LEFT/RIGHT along the
 * 8-row axis (this follows the physical orientation of the FITkit display). */
#define DIR_UP              0
#define DIR_LEFT            1
#define DIR_RIGHT           2
#define DIR_DOWN            3

/* Column decoder address lines A0..A3 (all on PTA). */
static const uint8_t COL_ADDR_PINS[4] = {8, 10, 6, 11};

/* Row select lines R0..R7 (all on PTA). */
static const uint8_t ROW_PINS[DISPLAY_ROWS] = {26, 24, 9, 25, 28, 7, 27, 29};

#define COL_MASK  (PIN(8) | PIN(10) | PIN(6) | PIN(11))
#define ROW_MASK  (PIN(26) | PIN(24) | PIN(9) | PIN(25) | \
                   PIN(28) | PIN(7)  | PIN(27) | PIN(29))

/* Display enable (active low) on PTE. */
#define DISPLAY_EN_PIN      28

/* Buttons on PORTE. */
#define BTN_RIGHT           10
#define BTN_RESET           11
#define BTN_DOWN            12
#define BTN_UP              26
#define BTN_LEFT            27

/* ------------------------------------------------------------------------- */
/* Game state (shared between main loop and button interrupt)                */
/* ------------------------------------------------------------------------- */

static volatile int x[SNAKE_LENGTH];         /* column of each segment (0..15) */
static volatile int y[SNAKE_LENGTH];         /* row of each segment (0..7)     */
static volatile int snakeDir[SNAKE_LENGTH];  /* direction of each segment      */

/**
 * @brief Puts the snake into its starting position and direction.
 */
static void resetSnake(void) {
    for (int i = 0; i < SNAKE_LENGTH; i++) {
        x[i] = START_X + i;
        y[i] = START_Y;
        snakeDir[i] = DIR_UP;
    }
}

/* ------------------------------------------------------------------------- */
/* Hardware setup                                                            */
/* ------------------------------------------------------------------------- */

/**
 * @brief Configures one PORTE pin as a button: pull-up, interrupt on falling edge.
 */
static void configureButton(int pin) {
    PORTE->PCR[pin] = PORT_PCR_ISF(0x01)     /* clear any stale flag          */
                    | PORT_PCR_IRQC(0x0A)    /* interrupt on falling edge     */
                    | PORT_PCR_MUX(0x01)     /* GPIO                          */
                    | PORT_PCR_PE(0x01)      /* pull enable                   */
                    | PORT_PCR_PS(0x01);     /* pull-up                       */
}

/**
 * @brief Enables clocks and configures all pins and the button interrupt.
 */
static void SystemConfig(void) {
    SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTE_MASK;

    /* Display pins -> GPIO */
    for (int i = 0; i < 4; i++) {
        PORTA->PCR[COL_ADDR_PINS[i]] = PORT_PCR_MUX(0x01);
    }
    for (int i = 0; i < DISPLAY_ROWS; i++) {
        PORTA->PCR[ROW_PINS[i]] = PORT_PCR_MUX(0x01);
    }
    PORTE->PCR[DISPLAY_EN_PIN] = PORT_PCR_MUX(0x01);

    /* Display pins are outputs; enable the display (#EN is active low). */
    PTA->PDDR = COL_MASK | ROW_MASK;
    PTE->PDDR = PIN(DISPLAY_EN_PIN);
    PTE->PDOR &= ~PIN(DISPLAY_EN_PIN);

    /* Buttons */
    configureButton(BTN_UP);
    configureButton(BTN_DOWN);
    configureButton(BTN_LEFT);
    configureButton(BTN_RIGHT);
    configureButton(BTN_RESET);

    NVIC_ClearPendingIRQ(PORTE_IRQn);
    NVIC_SetPriority(PORTE_IRQn, 0);
    NVIC_EnableIRQ(PORTE_IRQn);
}

/**
 * @brief Busy-wait delay (t1 * t2 loop iterations).
 */
static void delay(int t1, int t2) {
    for (volatile int i = 0; i < t1; i++) {
        for (volatile int j = 0; j < t2; j++);
    }
}

/* ------------------------------------------------------------------------- */
/* Display                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief Selects a column (0..15) by writing its binary value to the decoder.
 */
static void selectColumn(unsigned int column) {
    uint32_t bits = 0;

    for (int i = 0; i < 4; i++) {
        if (column & (1u << i)) {
            bits |= PIN(COL_ADDR_PINS[i]);
        }
    }
    PTA->PDOR = (PTA->PDOR & ~COL_MASK) | bits;
}

/**
 * @brief Activates exactly one row line (0..7).
 */
static void selectRow(unsigned int row) {
    if (row >= DISPLAY_ROWS) {
        return;
    }
    PTA->PDOR = (PTA->PDOR & ~ROW_MASK) | PIN(ROW_PINS[row]);
}

/**
 * @brief Draws the snake. Segments are lit one after another; refreshed fast
 *        enough, they all appear lit at the same time.
 */
static void snakeOutput(void) {
    for (int i = 0; i < SNAKE_LENGTH; i++) {
        selectColumn(x[i]);
        selectRow(y[i]);
        delay(SEGMENT_HOLD_OUTER, SEGMENT_HOLD_INNER);
    }
}

/* ------------------------------------------------------------------------- */
/* Game logic                                                                */
/* ------------------------------------------------------------------------- */

/**
 * @brief Modulo that also works for negative values (result is 0..n-1).
 */
static int wrap(int value, int n) {
    return ((value % n) + n) % n;
}

/**
 * @brief Changes the head direction unless it would reverse the snake.
 *        Opposite directions always sum to 3 (UP+DOWN, LEFT+RIGHT).
 */
static void turnSnake(int dir) {
    if (snakeDir[0] + dir == 3) {
        return;
    }
    snakeDir[0] = dir;
}

/**
 * @brief Moves every segment one step in its own direction (with wrap-around).
 */
static void moveSnake(void) {
    for (int i = 0; i < SNAKE_LENGTH; i++) {
        switch (snakeDir[i]) {
            case DIR_UP:    x[i] = wrap(x[i] - 1, DISPLAY_COLS); break;
            case DIR_DOWN:  x[i] = wrap(x[i] + 1, DISPLAY_COLS); break;
            case DIR_LEFT:  y[i] = wrap(y[i] + 1, DISPLAY_ROWS); break;
            case DIR_RIGHT: y[i] = wrap(y[i] - 1, DISPLAY_ROWS); break;
            default: break;
        }
    }
}

/**
 * @brief Passes the head direction down the body so the segments follow it.
 */
static void changeTailSnake(void) {
    for (int i = SNAKE_LENGTH - 1; i > 0; i--) {
        snakeDir[i] = snakeDir[i - 1];
    }
}

/* ------------------------------------------------------------------------- */
/* Interrupt handler                                                         */
/* ------------------------------------------------------------------------- */

/**
 * @brief Button handler (PORTE interrupt).
 */
void PORTE_IRQHandler(void) {
    uint32_t flags = PORTE->ISFR;

    if (flags & PIN(BTN_UP))    turnSnake(DIR_UP);
    if (flags & PIN(BTN_DOWN))  turnSnake(DIR_DOWN);
    if (flags & PIN(BTN_LEFT))  turnSnake(DIR_LEFT);
    if (flags & PIN(BTN_RIGHT)) turnSnake(DIR_RIGHT);
    if (flags & PIN(BTN_RESET)) resetSnake();

    PORTE->ISFR = flags;    /* flags are cleared by writing 1 */
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void) {
    SystemConfig();
    PTA->PDOR = 0x00;
    resetSnake();

    for (;;) {
        for (int i = 0; i < REFRESHES_PER_STEP; i++) {
            snakeOutput();
        }
        moveSnake();
        changeTailSnake();
        delay(STEP_DELAY_OUTER, STEP_DELAY_INNER);
    }

    return 0;
}
