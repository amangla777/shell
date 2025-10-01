#include "processSubstitution_tempfile.hh"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <vector>
#include <string>

// Global vector to record temporary file paths for cleanup.
static std::vector<std::string> tempFiles;

std::string create_process_substitution_tempfile(const std::string &subCmd) {
    // Create a unique temporary file.
    char templateFile[] = "/tmp/psXXXXXX";
    int fd = mkstemp(templateFile);
    if (fd < 0) {
        perror("mkstemp");
        return "";
    }
    // Do not unlink here because we want to use the file after the child writes to it.
    // The temporary filename is stored in templateFile.
    std::string tempFileName(templateFile);

    // Fork a child process to execute the sub-command.
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(fd);
        return "";
    } else if (pid == 0) {
        // In child: redirect stdout (and optionally stderr) to the temporary file.
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
        // Optionally redirect stderr:
        dup2(fd, STDERR_FILENO);
        close(fd);
        // Execute the sub-command using /bin/sh -c.
        execl("/bin/sh", "sh", "-c", subCmd.c_str(), (char *)NULL);
        // If execl returns, there was an error.
        perror("execl");
        _exit(1);
    }
    // In parent: close the fd. The child is writing to the file.
    close(fd);
    // Record the temporary filename for cleanup later.
    tempFiles.push_back(tempFileName);
    return tempFileName;
}

void cleanup_process_substitution_tempfiles() {
    for (const auto &file : tempFiles) {
        // Remove the temporary file.
        if (unlink(file.c_str()) < 0) {
            perror(("unlink " + file).c_str());
        }
    }
    tempFiles.clear();
}

