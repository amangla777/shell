
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>

/* 
 * Sets terminal into raw mode. 
 * This causes having the characters available
 * immediately instead of waiting for a newline. 
 * Also there is no automatic echo.
 */

static struct termios saved_tty_attributes;

void tty_raw_mode(void) {
    struct termios tty_attr;

    // Save current (cooked) mode
    tcgetattr(STDIN_FILENO, &saved_tty_attributes);
    // Make a copy to modify for raw mode
    tty_attr = saved_tty_attributes;

    // Disable canonical mode (ICANON) and echo (ECHO)
    tty_attr.c_lflag &= ~(ICANON | ECHO);

    // Minimum of 1 byte, no timer
    tty_attr.c_cc[VMIN]  = 1;
    tty_attr.c_cc[VTIME] = 0;

    // Apply changes immediately
    tcsetattr(STDIN_FILENO, TCSANOW, &tty_attr);
}

/*
 * tty_restore_mode():
 *   - Restore the original terminal attributes (cooked mode)
 */
void tty_restore_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty_attributes);
}
