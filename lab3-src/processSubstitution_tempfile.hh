#ifndef PROCESS_SUBSTITUTION_TEMPFILE_H
#define PROCESS_SUBSTITUTION_TEMPFILE_H

#include <string>

// Creates a temporary file, forks a child process that executes subCmd and writes
// its output to that file. Returns the filename (as a std::string) to be substituted.
// On error, returns an empty string.
std::string create_process_substitution_tempfile(const std::string &subCmd);

// Cleans up all temporary files created by process substitution.
void cleanup_process_substitution_tempfiles();

#endif

