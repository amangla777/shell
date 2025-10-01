%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
%}

%option noyywrap

%%
"<\\([^)]*\\)" {
    // If this rule matches, we print a debug line
    fprintf(stderr, "PSUB RULE MATCHED: %s\n", yytext);
}
"<" {
    // This rule matches a single '<'
    fprintf(stderr, "SINGLE-LESS-THAN RULE MATCHED\n");
}
.|\n {
    // Catch-all rule: do nothing
}

%%
int main(void) {
    yylex();
    return 0;
}

