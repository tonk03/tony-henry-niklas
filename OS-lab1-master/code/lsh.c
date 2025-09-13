/*
 * Main source code file for lsh shell program
 *
 * You are free to add functions to this file.
 * If you want to add functions in a separate file(s)
 * you will need to modify the CMakeLists.txt to compile
 * your additional file(s).
 *
 * Add appropriate comments in your code to make it
 * easier for us while grading your assignment.
 *
 * Using assert statements in your code is a great way to catch errors early and make debugging easier.
 * Think of them as mini self-checks that ensure your program behaves as expected.
 * By setting up these guardrails, you're creating a more robust and maintainable solution.
 * So go ahead, sprinkle some asserts in your code; they're your friends in disguise!
 *
 * All the best!
 */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

// The <unistd.h> header is your gateway to the OS's process management facilities.
#include <unistd.h>

#include "parse.h"

// Our imports and definitions
#include <errno.h>

static void print_cmd(Command *cmd);
static void print_pgm(Pgm *p);
void stripwhite(char *);

int main(void)
{
  for (;;)
  {
    char *line;
    /* 
     From the man7 docs:
     readline returns the text of the line read.  A blank line returns 
     the empty string.  If EOF is encountered while reading a line, and 
     the line is empty, readline returns NULL.  If an EOF is read with 
     a non-empty line, it is treated as a newline.
     
     To handle Ctrl+D properly we have to check if the return value is NULL.
    */
    line = readline("> ");
    if (line == NULL) {
      // If we get EOF (NULL) we are only supposed to exit according to the README.md
      // TODO: We have to check if that is enough, since this might leave some orphan processes.

      /*  
       From exit(3) docs:

       Signals sent to other processes
       If the exiting process is a session leader and its controlling
       terminal is the controlling terminal of the session, then each
       process in the foreground process group of this controlling
       terminal is sent a SIGHUP signal, and the terminal is
       disassociated from this session, allowing it to be acquired by a
       new controlling process.

       If the exit of the process causes a process group to become
       orphaned, and if any member of the newly orphaned process group is
       stopped, then a SIGHUP signal followed by a SIGCONT signal will be
       sent to each process in this process group.  See setpgid(2) for an
       explanation of orphaned process groups.
      */
      exit(EXIT_SUCCESS);
    }

    // Remove leading and trailing whitespace from the line
    stripwhite(line);

    // If stripped line not blank
    if (*line)
    {
      add_history(line);

      Command cmd;
      if (parse(line, &cmd) == 1)
      {
        // Just prints cmd
        print_cmd(&cmd);
        int pid = fork();
        if (pid == 0) {
            execvp(cmd.pgm->pgmlist[0], cmd.pgm->pgmlist);
            fprintf(stderr, "lsh: %s: fopen failed: %s\n", cmd.pgm->pgmlist[0], strerror(errno));
        } else {
            wait(&pid);
        }
      }
      else
      {
        printf("Parse ERROR\n");
      }
    }

    // Clear memory
    free(line);
  }

  return 0;
}

/*
 * Print a Command structure as returned by parse on stdout.
 *
 * Helper function, no need to change. Might be useful to study as inspiration.
 */
static void print_cmd(Command *cmd_list)
{
  printf("------------------------------\n");
  printf("Parse OK\n");
  printf("stdin:      %s\n", cmd_list->rstdin ? cmd_list->rstdin : "<none>");
  printf("stdout:     %s\n", cmd_list->rstdout ? cmd_list->rstdout : "<none>");
  printf("background: %s\n", cmd_list->background ? "true" : "false");
  printf("Pgms:\n");
  print_pgm(cmd_list->pgm);
  printf("------------------------------\n");
}

/* Print a (linked) list of Pgm:s.
 *
 * Helper function, no need to change. Might be useful to study as inpsiration.
 */
static void print_pgm(Pgm *p)
{
  if (p == NULL)
  {
    return;
  }
  else
  {
    char **pl = p->pgmlist;

    /* The list is in reversed order so print
     * it reversed to get right
     */
    print_pgm(p->next);
    printf("            * [ ");
    while (*pl)
    {
      printf("%s ", *pl++);
    }
    printf("]\n");
  }
}


/* Strip whitespace from the start and end of a string.
 *
 * Helper function, no need to change.
 */
void stripwhite(char *string)
{
  size_t i = 0;

  while (isspace(string[i]))
  {
    i++;
  }

  if (i)
  {
    memmove(string, string + i, strlen(string + i) + 1);
  }

  i = strlen(string) - 1;
  while (i > 0 && isspace(string[i]))
  {
    i--;
  }

  string[++i] = '\0';
}
