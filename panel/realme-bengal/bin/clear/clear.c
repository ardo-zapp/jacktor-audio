// Jacktor Audio Framework - Full Screen & Scrollback Clear
// Emits \033[3J (clear scrollback buffer) + \033[H\033[2J (home cursor + clear screen)

#include <stdio.h>
#include <unistd.h>

int main(void) {
    // \033[3J - Clear scrollback buffer (entire screen history)
    // \033[H  - Move cursor to home position (0,0)
    // \033[2J - Clear entire screen
    const char *clear_seq = "\033[3J\033[H\033[2J";
    write(STDOUT_FILENO, clear_seq, 11);
    return 0;
}