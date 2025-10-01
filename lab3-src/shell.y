/*
 * CS-252
 * shell.y: parser for shell
 *
 * This parser compiles the following grammar:
 *
 *   cmd [arg]* [ | cmd [arg]* ]* [ [> filename] [< filename] [2> filename]
 *          [>& filename] [>> filename] [>>& filename] ]* [&]
 *
 */

%code requires
{
#include <string>

#if __cplusplus > 199711L
#define register      // Deprecated in C++11 so remove the keyword
#endif
}

%union
{
  char        *string_val;
  std::string *cpp_string;
}

%token <cpp_string> WORD
%token NOTOKEN GREAT NEWLINE PIPE LT TWOGREAT ANDGREAT APPEND APPEND_AND AMPERSAND

%{
#include <cstdio>
#include "shell.hh"
#include <cstring>

void yyerror(const char * s);
int yylex();
%}

%%

goal:
  commands
  ;

commands:
  command
  | commands command
  ;

command:
  simple_command
  ;

simple_command:
  pipeline io_redirect_list background_opt NEWLINE {
    // printf("   Yacc: Execute command\n");
    Shell::_currentCommand.execute();
  }
  | NEWLINE
  | error NEWLINE { yyerrok; }
  ;

pipeline:
   pipeline PIPE command_and_args
	 | command_and_args
  ;

command_and_args:
  command_word argument_list {
    Shell::_currentCommand.insertSimpleCommand( Command::_currentSimpleCommand );
  }
  ;

command_word:
  WORD {
    // printf("   Yacc: insert command \"%s\"\n", $1->c_str());
    if (strcmp($1->c_str(), "exit") == 0) {
      printf("Good Bye!!\n");
      exit(0);
    }
    Command::_currentSimpleCommand = new SimpleCommand();
    Command::_currentSimpleCommand->insertArgument( $1 );
  }
  ;

argument_list:
  argument_list argument
  | /* empty */
  ;

argument:
  WORD {
    // printf("   Yacc: insert argument \"%s\"\n", $1->c_str());
    Command::_currentSimpleCommand->insertArgument( $1 );
  }
  ;

io_redirect_list:
  io_redirect_list io_redirect
  | /* empty */
  ;

io_redirect:
  GREAT WORD {
    // printf("   Yacc: insert output \"%s\"\n", $2->c_str());
	  if (Shell::_currentCommand._outFile != nullptr) {
      fprintf(stderr, "Ambiguous output redirect.\n");
      exit(1);
    }
    Shell::_currentCommand._outFile = $2;
  }
  | LT WORD {
    // printf("   Yacc: insert input \"%s\"\n", $2->c_str());
		if (Shell::_currentCommand._inFile != NULL ){
		  printf("Ambiguous output redirect.\n");
		  exit(0);
	  }
    Shell::_currentCommand._inFile = $2;
  }
  | TWOGREAT WORD {
    // printf("   Yacc: insert error output \"%s\"\n", $2->c_str());
    Shell::_currentCommand._errFile = $2;
  }
  | ANDGREAT WORD {
    // printf("   Yacc: insert both output \"%s\"\n", $2->c_str());
		if (Shell::_currentCommand._outFile != nullptr) {
      fprintf(stderr, "Ambiguous output redirect.\n");
      exit(1);
    }
    Shell::_currentCommand._outFile = $2;
    Shell::_currentCommand._errFile = $2;
  }
  | APPEND WORD {
    // printf("   Yacc: append output \"%s\"\n", $2->c_str());
		if (Shell::_currentCommand._outFile != nullptr) {
      fprintf(stderr, "Ambiguous output redirect.\n");
      exit(1);
    }
    Shell::_currentCommand._outFile = $2;
    Shell::_currentCommand._appendOut = true;
  }
  | APPEND_AND WORD {
    // printf("   Yacc: append both output \"%s\"\n", $2->c_str());
		if (Shell::_currentCommand._outFile != nullptr) {
      fprintf(stderr, "Ambiguous output redirect.\n");
      exit(1);
    }
    Shell::_currentCommand._outFile = $2;
    Shell::_currentCommand._errFile = $2;
    Shell::_currentCommand._appendOut = true;
    Shell::_currentCommand._appendErr = true;
  }
  ;

background_opt:
  AMPERSAND {
    // printf("   Yacc: background command\n");
    Shell::_currentCommand._background = true;
  }
  | /* empty */
  ;

%%

void
yyerror(const char * s)
{
  // fprintf(stderr,"%s", s);
}

#if 0
main()
{
  yyparse();
}
#endif
