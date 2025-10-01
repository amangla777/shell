#define _POSIX_C_SOURCE 200809L
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "shell.hh"
#include <cstring>
#include <iostream>
#include <ostream>


#include <limits.h>

int yyparse(void);
extern void yyrestart(FILE *input);

/*
void Shell::prompt() {
    if (isatty(0)) {

		  std::cout << "myshell> " << std::flush;
    }
}*/

void Shell::prompt() {
    if (isatty(0)) {
        const char *promptEnv = getenv("PROMPT");
        if (promptEnv && promptEnv[0] != '\0')
            std::cout << promptEnv << " " << std::flush;
        else
            std::cout << "myshell> " << std::flush;
    }
}

// SIGINT handler: when Ctrl-C is pressed, print a newline and reprint the prompt.
void sigint_handler(int sig) {
    //printf("\n");
		const char nl = '\n';
		write(STDOUT_FILENO, &nl, 1);
    Shell::prompt();
}

// SIGCHLD handler: reap zombie processes.
void sigchld_handler(int sig) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {
        // For example: printf("[child exited]\n");
    }
}

// Setup signal handlers for SIGINT and SIGCHLD.
void setup_signal_handlers() {
    struct sigaction sa_int;
   /* sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;*/
		sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);
}

bool sourcingFile = false;

void source_shellrc() {
    FILE *rcFile = fopen(".shellrc", "r");
    if (!rcFile) return;
    sourcingFile = true;
    yyrestart(rcFile);
    yyparse();
    fclose(rcFile);
    yyrestart(stdin);
    sourcingFile = false;
}


/*
int main(int argc, char *argv[]) {
    // Set up signal handlers.
    setup_signal_handlers();


    char resolvedPath[PATH_MAX];
    if (argc > 0 && realpath(argv[0], resolvedPath) != NULL) {
        shellPath = resolvedPath;  // Set the absolute path here.
    } else {
        shellPath = (argc > 0) ? argv[0] : "./shell";
    }

 // Main loop: repeatedly prompt and parse commands.
    while (true) {
        Shell::prompt();
        if (yyparse() != 0) break; // Exit loop on parse error or EOF.
    }

    return 0;
}*/

int main(int argc, char *argv[]) {
    setup_signal_handlers();

    char resolvedPath[PATH_MAX];
    if (argc > 0 && realpath(argv[0], resolvedPath) != NULL) {
        shellPath = resolvedPath;  // Set absolute shell path.
    } else {
        shellPath = (argc > 0) ? argv[0] : "./shell";
    }

    source_shellrc();

    // Main loop: repeatedly prompt and parse commands.
    while (true) {
        Shell::prompt();
        if (yyparse() != 0) break;
    }
    return 0;
}

Command Shell::_currentCommand;

