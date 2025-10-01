/*
 * CS252: Systems Programming
 * Purdue University
 * Example that shows how to read one line with simple editing
 * using raw terminal and a working history implementation.
 */
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include "tty-raw-mode.h"

#define MAX_BUFFER_LINE 2048

extern void tty_raw_mode(void);

// Buffer where the current input line is stored
int line_length;
char line_buffer[MAX_BUFFER_LINE];
char right_buffer[MAX_BUFFER_LINE];
int right_length;

// Simple history array, plus a length to track how many commands are stored
char *history[128];
int history_length = 0;

/*
 * Print usage information for certain key combinations.
 */
void read_line_print_usage() {
    char *usage =
        "\n"
        " ctrl-?       Print usage\n"
        " Backspace    Deletes last character\n"
        " up arrow     See previous command in the history\n"
        " down arrow   See next command in the history\n"
        " left arrow   Move cursor left\n"
        " right arrow  Move cursor right\n"
        " ctrl-D       Delete character at the cursor\n"
        " ctrl-E       Go to end of the line\n"
        " ctrl-A       Go to start of the line\n"
        " ctrl-B       Clear the entire line\n"
				" TAB         Perform filename completion\n";
    write(1, usage, strlen(usage));
}

static void find_largest_prefix(char **arr, int count, char *out_prefix) {
    if (count <= 0) {
        out_prefix[0] = '\0';
        return;
    }
    // Start with arr[0] as the candidate prefix
    strcpy(out_prefix, arr[0]);
    for (int i = 1; i < count; i++) {
        int j = 0;
        while (out_prefix[j] && arr[i][j] && out_prefix[j] == arr[i][j]) {
            j++;
        }
        out_prefix[j] = '\0';
    }
}

/*
 * handle_tab_completion:
 *   - Finds the "prefix" of the current word
 *   - Scans the current dir for matches
 *   - If no matches, do nothing
 *   - If 1 match, auto-complete fully
 *   - If multiple matches, fill up to largest prefix, then list them
 */
void handle_tab_completion(char *buffer, int *cursor_pos) {
    int pos = *cursor_pos;
    // 1) find start of current "word"
    int start = pos - 1;
    while (start >= 0 && !isspace((unsigned char)buffer[start])) {
        start--;
    }
    start++;
    char prefix[256] = {0};
    int prefix_len = pos - start;
    if (prefix_len > 255) prefix_len = 255;
    strncpy(prefix, buffer + start, prefix_len);
    prefix[prefix_len] = '\0';

    // 2) gather matches in current dir
    DIR *d = opendir(".");
    if (!d) return;
    char *matches[1024];
    int match_count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        // skip hidden files unless prefix starts with '.'
        if (prefix[0] != '.' && entry->d_name[0] == '.') {
            continue;
        }
        if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
            if (match_count < 1023) {
                matches[match_count] = strdup(entry->d_name);
                match_count++;
            }
        }
    }
    closedir(d);
    matches[match_count] = NULL; // safety

    if (match_count == 0) {
        // no match => do nothing
    } 
    else if (match_count == 1) {
        // exactly one match => fill it
        const char *m = matches[0];
        int m_len = strlen(m);
        if (m_len > prefix_len) {
            int to_add = m_len - prefix_len;
            if (*cursor_pos + to_add < MAX_BUFFER_LINE - 1) {
                for (int i = prefix_len; i < m_len; i++) {
                    buffer[*cursor_pos] = m[i];
                    write(1, &m[i], 1);
                    (*cursor_pos)++;
                }
                buffer[*cursor_pos] = '\0';
            }
        }
    } 
    else {
        // multiple matches => partial fill up to largest prefix
        char bigprefix[256];
        find_largest_prefix(matches, match_count, bigprefix);
        int blen = strlen(bigprefix);
        if (blen > prefix_len) {
            int to_add = blen - prefix_len;
            if (*cursor_pos + to_add < MAX_BUFFER_LINE - 1) {
                for (int i = prefix_len; i < blen; i++) {
                    buffer[*cursor_pos] = bigprefix[i];
                    write(1, &bigprefix[i], 1);
                    (*cursor_pos)++;
                }
                buffer[*cursor_pos] = '\0';
            }
        }
        // *** NOTICE: we do NOT list the matches here ***
        // (This is what stops "all directories" from printing.)
    }

    // free matches
    for (int i = 0; i < match_count; i++) {
        free(matches[i]);
    }
}
/*
 * Read a single line with some basic editing features in raw mode.
 * Returns a pointer to the line that was read.
 */
char *read_line() {
    // Set the terminal in raw mode
    tty_raw_mode();

    // Initialize line and right-buffer lengths
    line_length = 0;
    right_length = 0;

    // A local “scroll” index for history. Starts at history_length (no item selected yet).
    int current_history_index = history_length;

    // Continuously read characters until Enter is pressed.
    while (1) {
        char ch;
        read(0, &ch, 1);

        if (ch == 9) {
            handle_tab_completion(line_buffer, &line_length);
            continue;
        }

        // -------------------------------
        // 1) Printable characters
        // -------------------------------
        if (ch >= 32 && ch != 127) {
            // Echo the character
            write(1, &ch, 1);

            // If the line is at max capacity, do not add more characters
            if (line_length + right_length == MAX_BUFFER_LINE - 2) {
                continue;
            }

            // If cursor is at the end, simply append.
            if (right_length == 0) {
                line_buffer[line_length] = ch;
                line_length++;
            } else {
                // Insert in the middle
                line_buffer[line_length] = ch;
                line_length++;
                // Reprint the characters on the right buffer so they remain visible
                for (int i = right_length - 1; i >= 0; i--) {
                    write(1, &right_buffer[i], 1);
                }
                // Move the cursor back to the insertion point
                for (int i = 0; i < right_length; i++) {
                    char back = 8; // backspace
                    write(1, &back, 1);
                }
            }
        }
        // -------------------------------
        // 2) Enter
        // -------------------------------
        else if (ch == 10) {
            // Enter (ASCII 10)
            // If there are characters in the right buffer, move them to the left buffer
            while (right_length > 0) {
                line_buffer[line_length] = right_buffer[right_length - 1];
                line_length++;
                right_length--;
            }
            // Echo a newline
            write(1, &ch, 1);
            break;
        }
        // -------------------------------
        // 3) CTRL-B: Clear the entire line
        // -------------------------------
        else if (ch == 2) {
            // Move cursor to end first (so that everything is in line_buffer)
            while (right_length > 0) {
                write(1, "\033[1C", 5); 
                line_buffer[line_length] = right_buffer[right_length - 1];
                line_length++;
                right_length--;
            }
            // Now backspace everything from the line buffer
            while (line_length > 0) {
                char back = 8;
                write(1, &back, 1);
                char space = ' ';
                write(1, &space, 1);
                write(1, &back, 1);
                line_length--;
            }
        }
        // -------------------------------
        // 4) CTRL-D: Delete character at the cursor (from the right buffer)
        // -------------------------------
        else if (ch == 4) {
            if (right_length == 0) {
                continue;
            }
            // Reprint right-buffer minus the top character, overwrite with space, backtrack
            for (int i = right_length - 2; i >= 0; i--) {
                write(1, &right_buffer[i], 1);
            }
            char space = ' ';
            write(1, &space, 1);
            for (int i = 0; i < right_length; i++) {
                char back = 8;
                write(1, &back, 1);
            }
            right_length--;
        }
        // -------------------------------
        // 5) CTRL-E: Move cursor to end
        // -------------------------------
        else if (ch == 5) {
            // Move everything from the right buffer back to the line buffer
            while (right_length > 0) {
                write(1, "\033[1C", 5);
                line_buffer[line_length] = right_buffer[right_length - 1];
                line_length++;
                right_length--;
            }
        }
        // -------------------------------
        // 6) CTRL-A: Move cursor to beginning
        // -------------------------------
        else if (ch == 1) {
            // Move everything from the line buffer to the right buffer (in reverse)
            while (line_length > 0) {
                char back = 8;
                write(1, &back, 1);
                right_buffer[right_length] = line_buffer[line_length - 1];
                right_length++;
                line_length--;
            }
        }
        // -------------------------------
        // 7) CTRL-?: Print usage
        // -------------------------------
        else if (ch == 31) {
            read_line_print_usage();
            // Clear the line buffer completely after printing usage
            line_buffer[0] = 0;
            line_length = 0;
            right_length = 0;
            break;
        }
        // -------------------------------
        // 8) Backspace (Delete char to the left)
        // -------------------------------
        else if (ch == 8 || ch == 127) {
            // If nothing to the left, do nothing
            if (line_length == 0) {
                continue;
            }
            if (right_length == 0) {
                // Move cursor left, clear char, move cursor left again
                char back = 8;
                write(1, &back, 1);
                char space = ' ';
                write(1, &space, 1);
                write(1, &back, 1);
                line_length--;
            } else {
                // Need to shift everything properly
                char back = 8;
                write(1, &back, 1);
                // Reprint the right-buffer characters
                for (int i = right_length - 1; i >= 0; i--) {
                    write(1, &right_buffer[i], 1);
                }
                // Overwrite the last char with space
                char space = ' ';
                write(1, &space, 1);
                // Move cursor left for everything just printed + 1
                for (int i = 0; i < right_length + 1; i++) {
                    back = 8;
                    write(1, &back, 1);
                }
                line_length--;
            }
        }
        // -------------------------------
        // 9) Escape sequence (arrow keys)
        // -------------------------------
        else if (ch == 27) {
            char ch1, ch2;
            read(0, &ch1, 1);
            read(0, &ch2, 1);

            if (ch1 == 91) {
                // ---------------------------
                // 9a) Up or Down arrow
                // ---------------------------
                if (ch2 == 65 || ch2 == 66) {
                    // 1) Move cursor to the end of the current line
                    while (right_length > 0) {
                        write(1, "\033[1C", 5);
                        line_buffer[line_length] = right_buffer[right_length - 1];
                        line_length++;
                        right_length--;
                    }
                    // 2) Erase the entire current line from the screen
                    for (int i = 0; i < line_length; i++) {
                        char back = 8; // backspace
                        write(1, &back, 1);
                    }
                    for (int i = 0; i < line_length; i++) {
                        char space = ' ';
                        write(1, &space, 1);
                    }
                    for (int i = 0; i < line_length; i++) {
                        char back = 8; // backspace
                        write(1, &back, 1);
                    }

                    // 3) Reset line_length to 0
                    line_length = 0;
                    line_buffer[0] = '\0';

                    // 4) Adjust our history index
                    if (ch2 == 65) {
                        // UP arrow
                        if (current_history_index > 0) {
                            current_history_index--;
                        }
                    } else {
                        // DOWN arrow
                        if (current_history_index < history_length) {
                            current_history_index++;
                        }
                    }

                    // 5) Copy the new history command into line_buffer
                    if (current_history_index < history_length) {
                        strcpy(line_buffer, history[current_history_index]);
                        line_length = strlen(line_buffer);
                    } else {
                        line_buffer[0] = '\0';
                        line_length = 0;
                    }
                    right_length = 0;

                    // 6) Print the newly selected history command
                    write(1, line_buffer, line_length);
                }
                // ---------------------------
                // 9b) Left arrow
                // ---------------------------
                else if (ch2 == 68) {
                    if (line_length == 0) {
                        continue;
                    }
                    // Move cursor left by 1
                    char back = 8;
                    write(1, &back, 1);
                    // Move one char from line_buffer to right_buffer
                    right_buffer[right_length] = line_buffer[line_length - 1];
                    right_length++;
                    line_length--;
                }
                // ---------------------------
                // 9c) Right arrow
                // ---------------------------
                else if (ch2 == 67) {
                    if (right_length == 0) {
                        continue;
                    }
                    // Move cursor right (visually)
                    write(1, "\033[1C", 5);
                    // Move one char from right_buffer back to line_buffer
                    line_buffer[line_length] = right_buffer[right_length - 1];
                    line_length++;
                    right_length--;
                }
            }
        }
    }

    // Append newline and a null terminator to line_buffer
    line_buffer[line_length] = '\n';
    line_length++;
    line_buffer[line_length] = '\0';

    // For storing in history, remove the trailing newline so re-echo does not include it
    if (line_length > 0 && line_buffer[line_length - 1] == '\n') {
        line_buffer[line_length - 1] = '\0';
        line_length--;
    }

    // Add the line to history (allocate space and copy), if not empty
    if (line_length > 0) {
        history[history_length] = (char *)malloc(strlen(line_buffer) + 1);
        strcpy(history[history_length], line_buffer);
        history_length++;
    }

    // Put the newline character back so the caller sees it
    line_buffer[line_length] = '\n';
    line_length++;
    line_buffer[line_length] = '\0';

		tty_restore_mode();

    return line_buffer;
}

