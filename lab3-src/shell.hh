#ifndef shell_hh
#define shell_hh

#include "command.hh"

extern std::string shellPath;
extern int lastCommandExit;
extern int lastBgPID;
extern std::string lastArgument;
extern bool sourcingFile;

struct Shell {

  static void prompt();

  static Command _currentCommand;
};

#endif
