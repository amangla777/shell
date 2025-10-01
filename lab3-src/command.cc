/*
 * CS252: Shell project
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 * DO NOT PUT THIS PROJECT IN A PUBLIC REPOSITORY LIKE GIT. IF YOU WANT 
 * TO MAKE IT PUBLICLY AVAILABLE YOU NEED TO REMOVE ANY SKELETON CODE 
 * AND REWRITE YOUR PROJECT SO IT IMPLEMENTS FUNCTIONALITY DIFFERENT THAN
 * WHAT IS SPECIFIED IN THE HANDOUT. WE OFTEN REUSE PART OF THE PROJECTS FROM  
 * SEMESTER TO SEMESTER AND PUTTING YOUR CODE IN A PUBLIC REPOSITORY
 * MAY FACILITATE ACADEMIC DISHONESTY.
 */

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>
#include <cstring>
#include <pwd.h>
#include <regex>
#include <vector>
#include <dirent.h>
#include <algorithm>
#include <sstream>
#include <sys/stat.h>


#include "command.hh"
#include "shell.hh"




using namespace std;

std::string shellPath = "";      // Absolute path of the shell executable.
int lastCommandExit = 0;         // Exit status of last foreground command.
int lastBgPID = 0;               // PID of last process run in background.
std::string lastArgument = "";   // Last argument from the fully expanded previous command.


extern bool sourcingFile;



// Expand tilde expressions.
string expand_tilde(const string &input) {
    if (input.empty() || input[0] != '~')
        return input;
    string result;
    if (input.size() == 1 || input[1] == '/') {
        char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : const_cast<char*>("");
        }
        result = home;
        result += input.substr(1);
    } else {
        size_t pos = input.find('/');
        string username = (pos == string::npos) ? input.substr(1) : input.substr(1, pos - 1);
        struct passwd *pw = getpwnam(username.c_str());
        if (pw) {
            result = pw->pw_dir;
            if (pos != string::npos)
                result += input.substr(pos);
        } else {
            result = input;
        }
    }
    return result;
}

string expand_env(const string &input, const string &prevLastArg) {
    string output = input;
    // Regular expression to match ${VAR}
    regex var_regex("\\$\\{([^}\\s]+)\\}");
    smatch match;
    // Loop until no more variable expressions are found.
    while (regex_search(output, match, var_regex)) {
        string varName = match[1].str();
        string varValue;
        if (varName == "$")
            varValue = to_string(getpid());
        else if (varName == "?")
            varValue = to_string(lastCommandExit);
        else if (varName == "!")
            varValue = to_string(lastBgPID);
        else if (varName == "_")
            varValue = prevLastArg;
        else if (varName == "SHELL")
            // Here is the key: return shellPath (which must be set in main).
            varValue = shellPath;
        else {
            char *env = getenv(varName.c_str());
            if (env)
                varValue = env;
        }
        output = match.prefix().str() + varValue + match.suffix().str();
    }
    return output;
}

// --- [Wildcard Expansion] ---

// Helper: split a path into components based on '/'
vector<string> splitPath(const string &path) {
    vector<string> components;
    istringstream iss(path);
    string token;
    while (getline(iss, token, '/')) {
        components.push_back(token);
    }
    return components;
}

// [Recursive Wildcard Expansion] – recursive function using size_t for index.
vector<string> recursiveExpand(const vector<string> &components, size_t index, const string &prefix) {
    vector<string> results;
    if (index >= components.size())
        return { prefix };

    string comp = components[index];
    if (comp.empty())
        return recursiveExpand(components, index + 1, prefix);

    // If component has no wildcard, simply append.
    if (comp.find('*') == string::npos && comp.find('?') == string::npos) {
        string newPrefix = prefix;
        if (prefix != "/")
            newPrefix += "/";
        newPrefix += comp;
        return recursiveExpand(components, index + 1, newPrefix);
    }
    
    // Component contains wildcard(s); open current directory.
    DIR *dirPtr = opendir(prefix.c_str());
    if (!dirPtr)
        return results;
    
    // Convert wildcard pattern into regex.
    string regex_pattern = "^";
    for (char c : comp) {
        if (c == '*')
            regex_pattern += ".*";
        else if (c == '?')
            regex_pattern += ".";
        else if (ispunct(c))
            regex_pattern += "\\" + string(1, c);
        else
            regex_pattern.push_back(c);
    }
    regex_pattern += "$";
    regex re(regex_pattern);
    
    struct dirent *entry;
    while ((entry = readdir(dirPtr)) != nullptr) {
        string fname = entry->d_name;
        // If comp does not start with '.', skip hidden files.
        if (!comp.empty() && comp[0] != '.' && !fname.empty() && fname[0] == '.')
            continue;
        try {
            if (regex_match(fname, re)) {
                string newPrefix = prefix;
                if (prefix != "/")
                    newPrefix += "/";
                newPrefix += fname;
                vector<string> tail = recursiveExpand(components, index + 1, newPrefix);
                results.insert(results.end(), tail.begin(), tail.end());
            }
        } catch (regex_error &e) {
            continue;
        }
    }
    closedir(dirPtr);
    return results;
}

// Main wildcard expansion function.
// If the pattern is relative (start = "."), we remove leading "./" from results.
string expand_wildcard(const string &input) {
    if (input.find('*') == string::npos && input.find('?') == string::npos)
        return input;
    
    vector<string> components = splitPath(input);
    string start;
    bool isAbsolute = false;
    if (!input.empty() && input[0] == '/') {
        start = "/";
        isAbsolute = true;
    } else {
        start = ".";
    }
    
    vector<string> expansion = recursiveExpand(components, 0, start);
    if (expansion.empty())
        return input;
    
    sort(expansion.begin(), expansion.end());
    ostringstream oss;
    for (size_t i = 0; i < expansion.size(); i++) {
        string token = expansion[i];
        // [Wildcard Fix]: If the input was relative, strip leading "./"
        if (!isAbsolute && token.compare(0, 2, "./") == 0)
            token = token.substr(2);
        oss << token;
        if (i < expansion.size() - 1)
            oss << " ";
    }
    return oss.str();
}

// Combined expansion for an argument (including tilde/wildcard as needed)
string expand_argument(const string &arg, const string &prevLastArg) {
    // (Assume you already have functions expand_tilde() and expand_wildcard())
    string tmp = expand_tilde(arg);
    tmp = expand_env(tmp, prevLastArg);
    tmp = expand_wildcard(tmp);
    return tmp;
}


Command::Command() {
    // Initialize a new vector of Simple Commands
    _simpleCommands = std::vector<SimpleCommand *>();

    _outFile = NULL;
    _inFile = NULL;
    _errFile = NULL;
    _background = false;
    _appendOut = false;
    _appendErr = false;
}

void Command::insertSimpleCommand( SimpleCommand * simpleCommand ) {
    // add the simple command to the vector
    _simpleCommands.push_back(simpleCommand);
}

void Command::clear() {
    // 1) Delete the SimpleCommands
    for (auto simpleCommand : _simpleCommands) {
        delete simpleCommand;
    }
    _simpleCommands.clear();

    // 2) Free outFile and errFile carefully
    if (_outFile == _errFile && _outFile != nullptr) {
        // Both point to the same memory => free once
        delete _outFile;
        _outFile = nullptr;
        _errFile = nullptr;
    } else {
        if (_outFile) {
            delete _outFile;
            _outFile = nullptr;
        }
        if (_errFile) {
            delete _errFile;
            _errFile = nullptr;
        }
    }

    // 3) Free inFile if needed
    if (_inFile) {
        delete _inFile;
        _inFile = nullptr;
    }

    // 4) Reset flags
    _background = false;
    _appendOut = false;
    _appendErr = false;
}


void Command::print() {
    /*
    printf("\n\n");
    printf("              COMMAND TABLE                \n\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    int i = 0;
    for (auto & simpleCommand : _simpleCommands) {
        printf("  %-3d ", i++);
        simpleCommand->print();
    }

    printf("\n\n");
    printf("  Output       Input        Error        Background\n");
    printf("  ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s\n",
           _outFile ? _outFile->c_str() : "default",
           _inFile  ? _inFile->c_str()  : "default",
           _errFile ? _errFile->c_str() : "default",
           _background ? "YES" : "NO");
    printf("\n\n");
		*/
}

void Command::execute() {
    if (_simpleCommands.empty()) {
        Shell::prompt();
        return;
    }

    // Preserve previous command's last argument for ${_} expansion.
    string prevLastArg = lastArgument;

    // Perform expansion on each argument without updating lastArgument from the current command.
    for (size_t i = 0; i < _simpleCommands.size(); i++) {
        for (size_t j = 0; j < _simpleCommands[i]->_arguments.size(); j++) {
            string original = *(_simpleCommands[i]->_arguments[j]);
            string expanded = expand_argument(original, prevLastArg);
            delete _simpleCommands[i]->_arguments[j];
            _simpleCommands[i]->_arguments[j] = new string(expanded);
        }
    }

    // Handle built-in commands (only if exactly one simple command)
    string cmd = *(_simpleCommands[0]->_arguments[0]);
    if (_simpleCommands.size() == 1 &&
        (cmd == "printenv" || cmd == "setenv" || cmd == "unsetenv"
          || cmd == "cd" || cmd == "exit")) {

        if (cmd == "printenv") {
            extern char **environ;
            for (int i = 0; environ[i] != NULL; i++) {
                printf("%s\n", environ[i]);
            }
            fflush(stdout);
            lastCommandExit = 0;
        } else if (cmd == "setenv") {
            if (_simpleCommands[0]->_arguments.size() < 3) {
                fprintf(stderr, "Usage: setenv VARIABLE VALUE\n");
                lastCommandExit = 1;
            } else {
                const char *var = _simpleCommands[0]->_arguments[1]->c_str();
                const char *val = _simpleCommands[0]->_arguments[2]->c_str();
                if (setenv(var, val, 1) != 0) {
                    perror("setenv");
                    lastCommandExit = 1;
                } else {
                    lastCommandExit = 0;
                }
            }
        } else if (cmd == "unsetenv") {
            if (_simpleCommands[0]->_arguments.size() < 2) {
                fprintf(stderr, "Usage: unsetenv VARIABLE\n");
                lastCommandExit = 1;
            } else {
                const char *var = _simpleCommands[0]->_arguments[1]->c_str();
                if (unsetenv(var) != 0) {
                    perror("unsetenv");
                    lastCommandExit = 1;
                } else {
                    lastCommandExit = 0;
                }
            }
        } else if (cmd == "cd") {
            const char *path = nullptr;
            if (_simpleCommands[0]->_arguments.size() < 2) {
                path = getenv("HOME");
                if (!path) {
                    fprintf(stderr, "cd: HOME not set\n");
                    lastCommandExit = 1;
                }
            } else {
                path = _simpleCommands[0]->_arguments[1]->c_str();
            }
            if (path && chdir(path) != 0) {
                fprintf(stderr, "cd: can't cd to %s\n", path);
                lastCommandExit = 1;
            } else {
                lastCommandExit = 0;
            }
        } else if (cmd == "exit") {
            printf("Good bye!!\n");
            exit(0);
        }
        clear();
        if (isatty(0))
            Shell::prompt();
        return;
    }

    // For non built-in commands (or pipelines), set up I/O redirection.
    int dfltin  = dup(0);
    int dfltout = dup(1);
    int dflterr = dup(2);
    int fdin = 0, fdout = 0, fderr = 0;

    if (_inFile) {
        fdin = open(_inFile->c_str(), O_RDONLY);
        if (fdin < 0) {
            perror("open input file");
            dup2(dfltin, 0);
            dup2(dfltout, 1);
            dup2(dflterr, 2);
            close(dfltin); close(dfltout); close(dflterr);
            clear();
            Shell::prompt();
            return;
        }
    } else {
        fdin = dup(dfltin);
    }

    pid_t pid;
    for (size_t i = 0; i < _simpleCommands.size(); i++) {
        dup2(fdin, 0);
        close(fdin);
        if (i == _simpleCommands.size() - 1) {
            if (_outFile) {
                if (_appendOut)
                    fdout = open(_outFile->c_str(), O_WRONLY | O_APPEND | O_CREAT, 0600);
                else
                    fdout = open(_outFile->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fdout < 0) {
                    perror("open output file");
                    clear();
                    Shell::prompt();
                    return;
                }
            } else {
                fdout = dup(dfltout);
            }
            if (_errFile) {
                int flags = O_WRONLY | O_CREAT;
                flags |= (_appendOut) ? O_APPEND : O_TRUNC;
                fderr = open(_errFile->c_str(), flags, 0666);
                if (fderr < 0) {
                    perror("open error file");
                    clear();
                    Shell::prompt();
                    return;
                }
            } else {
                fderr = dup(dflterr);
            }
        } else {
            int fdpipe[2];
            if (pipe(fdpipe) < 0) {
                perror("pipe");
                clear();
                Shell::prompt();
                return;
            }
            fdout = fdpipe[1];
            fdin = fdpipe[0];
            fderr = dup(dflterr);
        }
        dup2(fdout, 1);
        close(fdout);
        dup2(fderr, 2);
        close(fderr);
        pid = fork();
        if (pid == -1) {
            perror("fork");
            clear();
            Shell::prompt();
            return;
        }
        if (pid == 0) { // Child process.
            size_t argsize = _simpleCommands[i]->_arguments.size();
            char **x = new char*[argsize + 1];
            for (size_t j = 0; j < argsize; j++) {
                x[j] = const_cast<char*>(_simpleCommands[i]->_arguments[j]->c_str());
            }
            x[argsize] = NULL;
            // [Optional: Hack for "printenv" if needed]
            if (strcmp(x[0], "printenv") == 0) {
                extern char **environ;
                for (int k = 0; environ[k] != NULL; k++) {
                    printf("%s\n", environ[k]);
                }
                fflush(stdout);
                _exit(0);
            }
            execvp(x[0], x);
            perror("execvp failed");
            _exit(1);
        }
    } // end for

    dup2(dfltin, 0);
    dup2(dfltout, 1);
    dup2(dflterr, 2);
    close(dfltin); close(dfltout); close(dflterr);

    if (!_background) {
        int status;
        waitpid(pid, &status, 0);
        lastCommandExit = WEXITSTATUS(status);
    }
    else
        cout << "[process id " << pid << "]" << endl;

    // [Change for ${_}]: Update lastArgument with the last argument of the current command.
    if (!_simpleCommands.empty() && !_simpleCommands.back()->_arguments.empty())
        lastArgument = *(_simpleCommands.back()->_arguments.back());
    
    clear();
    if (!sourcingFile && isatty(0))


        Shell::prompt();
}

SimpleCommand * Command::_currentSimpleCommand;

