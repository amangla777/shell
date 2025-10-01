
/*
 *
 * CS-252 
 * shell.l: lexical analyzer for shell
 * Extended to support additional tokens for Part 1A
 *
 */

%{
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>
#include <vector>

#include "y.tab.hh"

struct ProcessSubEntry {
    std::string fifoPath;
    std::string tempDir;
};

std::vector<ProcessSubEntry> processSubList;

#define MAX_INCLUDE_DEPTH 10
YY_BUFFER_STATE include_stack[MAX_INCLUDE_DEPTH];
int include_stack_ptr = 0;

extern "C" char * read_line();


int mygetc(FILE * f) {
  static char *p;
  char ch;

     if (!isatty(0)) {
  // stdin is not a tty. Call real getc
    return getc(f);
  }

  // stdin is a tty. Call our read_line.
  if (p==NULL || *p == 0) {
    char * s = read_line();
    p = s;
  }

  ch = *p;
  p++;

  return ch;
}

#undef getc
#define getc(f) mygetc(f)


static void yyunput (int c, char *buf_ptr);

void myunputc(int c) {
  unput(c);
}

void process_source_file(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", fname);
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        if (len > 0 && line[len-1]=='\n')
            line[len-1] = '\0';
        if (strncmp(line, "setenv", 6) == 0) {
            char var[256], val[256];
            if (sscanf(line, "setenv %s %s", var, val) == 2)
                setenv(var, val, 1);
        } else if (strncmp(line, "echo", 4) == 0) {
            char *msg = line + 4;
            while (*msg && isspace(*msg))
                msg++;
            if (msg[0]=='\"') {
                msg++;
                char *q = strchr(msg, '\"');
                if (q)
                    *q = '\0';
            }
            printf("%s\n", msg);
        }
    }
    fclose(f);
}

%}

%option noyywrap



%%

^source[ \t]+([^ \t\n]+)[ \t]*\n {
    char fname[256];
    /* Extract filename from "source filename" */
    sscanf(yytext, "source %s", fname);
    if (include_stack_ptr >= MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "Includes nested too deeply\n");
        exit(1);
    }
    /* Push the current buffer onto our manual stack */
    include_stack[include_stack_ptr++] = YY_CURRENT_BUFFER;
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", fname);
        /* Do nothing further here; scanning continues from the current buffer */
    } else {
        /* Create a new buffer for the file and switch to it */
        yy_switch_to_buffer( yy_create_buffer(fp, YY_BUF_SIZE) );
    }
}

<<EOF>> {
    if (--include_stack_ptr < 0) {
        yyterminate();  /* No more buffers, exit scanning. */
    } else {
        yy_delete_buffer(YY_CURRENT_BUFFER);
        yy_switch_to_buffer(include_stack[include_stack_ptr]);
    }
}


$\([^\n]*\) {
    std::string fullCommand = yytext; 
    std::string innerCommand = fullCommand.substr(2, fullCommand.size() - 3);
    int saved_stdin  = dup(0);
    int saved_stdout = dup(1);
    int pipeForSubshell[2], pipeForOutput[2];
    if (pipe(pipeForSubshell) < 0 || pipe(pipeForOutput) < 0) {
        perror("pipe");
        exit(1);
    }
    write(pipeForSubshell[1], innerCommand.c_str(), innerCommand.size());
    write(pipeForSubshell[1], "\n", 1);
    close(pipeForSubshell[1]);
    dup2(pipeForSubshell[0], 0);
    close(pipeForSubshell[0]);
    dup2(pipeForOutput[1], 1);
    close(pipeForOutput[1]);
    int childPid = fork();
    if (childPid == 0) {
        execvp("/proc/self/exe", NULL);
        _exit(1);
    } else if (childPid < 0) {
        perror("fork");
        exit(1);
    }
    dup2(saved_stdin, 0);
    dup2(saved_stdout, 1);
    close(saved_stdin);
    close(saved_stdout);
    char ch;
    char *outputBuffer = (char*) malloc(4096);
    int pos = 0;
    while (read(pipeForOutput[0], &ch, 1) > 0) {
        outputBuffer[pos++] = (ch == '\n') ? ' ' : ch;
    }
    outputBuffer[pos] = '\0';
    for (int k = pos - 1; k >= 0; k--) {
        myunputc(outputBuffer[k]);
    }
    free(outputBuffer);
}

\<\([^\n)]*\)    {
    // Extract the inner command.
    std::string fullCommand = yytext;
    // Remove the leading "<(" and the trailing ")"
    std::string innerCommand = fullCommand.substr(2, fullCommand.size() - 3);

    // Create a temporary directory for the FIFO.
    char tempDirTemplate[] = "/tmp/procsubXXXXXX";
    char *temp_dir_ptr = mkdtemp(tempDirTemplate);
    if (!temp_dir_ptr) {
        perror("mkdtemp");
        exit(1);
    }
    // Copy the directory name into a std::string.
    std::string tempDir(temp_dir_ptr);

    // Build the FIFO path inside the temporary directory.
    char fifo_path[512];
    snprintf(fifo_path, sizeof(fifo_path), "%s/fifo", temp_dir_ptr);

    // Create the FIFO (named pipe) with mode 0600.
    if (mkfifo(fifo_path, 0600) < 0) {
        perror("mkfifo");
        exit(1);
    }

    // Fork a child process to execute the inner command.
    int pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        // In child: Open FIFO for writing.
        int fifo_fd = open(fifo_path, O_WRONLY);
        if (fifo_fd < 0) {
            perror("open fifo for writing");
            exit(1);
        }
        // Redirect child's stdout to the FIFO.
        dup2(fifo_fd, STDOUT_FILENO);
        close(fifo_fd);

        // Execute the inner command via the shell.
        execlp("sh", "sh", "-c", innerCommand.c_str(), (char*)NULL);
        perror("execlp");
        exit(1);
    }
    // In parent: Save information for later cleanup.
    processSubList.push_back(ProcessSubEntry{ std::string(fifo_path), tempDir });

    // Return the FIFO name as a WORD token so that the calling command sees it as a file.
    yylval.cpp_string = new std::string(fifo_path);
    return WORD;
}



\"([^\"\n]*)\"    { 
                      /* Match double-quoted strings, remove the quotes */
                      yylval.cpp_string = new std::string(yytext + 1, yyleng - 2);
                      return WORD;
                   }
\'([^\'\n]*)\'    { 
                      /* Match single-quoted strings, remove the quotes */
                      yylval.cpp_string = new std::string(yytext + 1, yyleng - 2);
                      return WORD;
                   }
[^ \t\n|><&]*\\[^ \t\n]* {
	/* 2.5 Escaping */

	char * str = strdup(yytext);
	char * newstr = (char*) malloc (100);


	int i = 0;
	while (*str){
		if (*str == '\\'){
			if (*(str+1) == '\\'){
				str = str +2;
				newstr[i++] = '\\';
			}else{
				newstr[i++] = *(++str);
			}
		}else{
			newstr[i++] = *str;
		}
		str++;
	}
	newstr[i] = '\0';


	yylval.cpp_string = new std::string(newstr);

	return WORD;
}


">>&"       { return APPEND_AND; }
">>"        { return APPEND; }
">&"        { return ANDGREAT; }
"2>"        { return TWOGREAT; }
">"         { return GREAT; }
"<"         { return LT; }
"|"         { return PIPE; }
"&"         { return AMPERSAND; }
\n          { return NEWLINE; }
[ \t]+      { /* Discard spaces and tabs */ }
[^ \t\n><|&]+  {
  yylval.cpp_string = new std::string(yytext);
  return WORD;
}
.           { /* catch any unrecognized character */ }
%%


