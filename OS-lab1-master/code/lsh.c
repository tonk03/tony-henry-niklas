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
#include <stdbool.h>
#include <linux/limits.h> /* Used to read current working directory */
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/types.h>
#include <signal.h> /* Used to handle Ctrl+C */
#include <sys/wait.h>

// The <unistd.h> header is your gateway to the OS's process management facilities.
#include <unistd.h>

#include "parse.h"

#define MAX_TOKENS 100 // Number of tokens max for a command

static void run_cmd(Command cmd);
static void run_piped_cmd(Command cmd);
static void child_exec(Pgm *pgm, Command *cmd,
                       int fd_in, int fd_out,
                       int is_first, int is_last,
                       int close_fd1, int close_fd2);
static void print_cmd(Command *cmd);
static void print_pgm(Pgm *p);
void stripwhite(char *);

// Terminal colors
typedef enum
{
  BLACK = 30,
  RED,
  GREEN,
  YELLOW,
  BLUE,
  MAGENTA,
  CYAN,
  WHITE
} Color;

void print_color(const char *text, Color fg, int bold)
{
  if (bold)
    printf("\033[1;%dm%s\033[0m", fg, text);
  else
    printf("\033[%dm%s\033[0m", fg, text);
}

void cleanup_and_exit(int code)
{
  int status;

  // Reap already-terminated children
  while (waitpid(-1, &status, WNOHANG) > 0)
  {
    // nothing
  }

  exit(code);
}

// Used to make error codes red
void perror_red(const char *msg)
{
  fprintf(stderr, "\033[31m"); // red
  perror(msg);
  fprintf(stderr, "\033[0m"); // reset
}

void sigint_handler(int sig)
{
  // fprintf(stderr, "\033[0m"); // red
  printf("\nCaught SIGINT (%d). Use 'exit' or 'Ctrl+D' to quit shell.\n", sig);
  print_color("> ", BLUE, 1);
}

// Handles background children
void sigchld_handler(int sig)
{
  (void)sig;
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
  {
  }
  // fprintf(stderr, "\033[0m");
}

int main(int argc, char *argv[]) // argc is number of arguments. argv contains or arguments. first argument is the program name
{
  // Program arguments
  bool debug = false;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--debug") == 0)
    {
      debug = true;
    }
    else if (strcmp(argv[i], "--help") == 0)
    {
      printf("Usage: %s [options]\n", argv[0]);
      printf("Options:\n");
      printf("  --debug   Enable debugging output\n");
      printf("  --help    Show this help message\n");
      return 0;
    }
    else
    {
      printf("Unknown argument: %s\n", argv[i]);
      printf("Try '%s --help' for available options\n", argv[0]);
      return 1;
    }
  }
  signal(SIGINT, sigint_handler);   // ignore Ctrl-C
  signal(SIGCHLD, sigchld_handler); // Auto kill bg processes to avoid zombies

  system("clear"); // System not allowedfor lab TODO look into how to clear without system (maybe just print like 20 newlines)
  print_color("----- Welcome to our terminal! -----\n----------------------------------------\n", YELLOW, 1);
  for (;;)
  {
    char *line;

    print_color("> ", BLUE, 1);
    line = readline("");

    // Ctrl+D
    if (line == NULL)
    {
      printf("\n");
      cleanup_and_exit(0);
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
        run_cmd(cmd);
        if (debug)
          print_cmd(&cmd);
      }
      else
      {
        print_color("Parse ERROR\n", RED, 1);
      }
    }
    else
    {
      printf("\n");
    }

    // Clear memory
    free(line);
  }

  return 0;
}

// Runs a given command
static void run_cmd(Command cmd)
{
  // Check if we perform a command that should not be created in a child process
  if (strcmp(cmd.pgm->pgmlist[0], "cd") == 0)
  { // strcmp returns 0 if both strings are equal :/
    // Change working directory
    char *path = cmd.pgm->pgmlist[1];
    if (path == NULL)
      path = getenv("HOME");
    if (chdir(path) != 0)
    {
      perror_red("chdir failed");
    }
    // Print current path
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
      print_color(strcat(cwd, "\n"), BLUE, 1); // %s formats the cwd
    }
  }
  else if (strcmp(cmd.pgm->pgmlist[0], "exit") == 0)
  {
    cleanup_and_exit(0);
  }

  else if (cmd.pgm->next != NULL) // There is more than one Pgm, so it is a pipeline
  {
    run_piped_cmd(cmd);
  }
  // Command that should be sent to a subprocess
  else
  {
    // Child pid is an integer value
    // fork() in C spawns a new thread at the specific line
    // This means we use pid id to keep track of our processes
    pid_t pid = fork();
    if (pid == 0)
    {
      // delegate all child setup (pgid, SIGINT, redirections, exec) to helper
      child_exec(cmd.pgm, &cmd, -1, -1, 1, 1, -1, -1);
    }
    else if (pid < 0)
    {
      perror_red("fork failed");
    }
    else if (pid > 0)
    {
      // Parent process
      if (!cmd.background)
      { // If there is no child process we wait for the child process to finish
        int status;
        waitpid(pid, &status, 0);
      }
      // fprintf(stderr, "\033[0m"); // Reset color
    }
  }
}

static void run_piped_cmd(Command cmd)
{
  // step 1: walk through the linked list of programs we got from the parser.
  // the parser gives them in reverse order (the last command in the pipeline is the head in the list).
  // so here we copy them into a temporary array in that parser order.
  Pgm *parser_list[64]; // max list size of 64 only arbitrarily 
  int count = 0;
  for (Pgm *q = cmd.pgm; q != NULL; q = q->next) {
    if (count >= 64) {
      fprintf(stderr, "pipeline too long\n");
      return;
    }
    parser_list[count++] = q; // parser_list[0] = rightmost, parser_list[count-1] = leftmost
  }

  // step 2: reverse the temporary array so we get a normal left-to-right order
  // now command_list[0] really is the leftmost program in the pipeline,
  // command_list[count-1] is the rightmost program.
  Pgm *command_list[count];
  for (int i = 0; i < count; ++i) {
    command_list[i] = parser_list[count - 1 - i];
  }

  int prev_fd_read = -1;     // this keeps track of the read end of the previous pipe
  pid_t children[64];
  int child_count = 0;

  // step 3: go through each program from left to right and set up pipes + forks
  for (int i = 0; i < count; ++i) {
    int fd[2] = { -1, -1 };
    int not_last = (i < count - 1); // true if this is not the rightmost command

    // if not the last command, create a pipe so its output can feed the next command
    if (not_last) {
      if (pipe(fd) < 0) {
        perror_red("pipe");
        if (prev_fd_read != -1) close(prev_fd_read);
        return;
      }
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror_red("fork");
      if (prev_fd_read != -1) close(prev_fd_read);
      if (not_last) { close(fd[0]); close(fd[1]); }
      return;
    }

    if (pid == 0) {
      // delegate all child setup (pgid, SIGINT, pipes/files, exec) to helper
      child_exec(
        command_list[i], &cmd,
        /* fd_in  */ prev_fd_read,
        /* fd_out */ not_last ? fd[1] : -1,
        /* first? */ i == 0,
        /* last?  */ i == count - 1,
        /* close_fd1 (e.g., current pipe read end) */ not_last ? fd[0] : -1,
        /* close_fd2 (optionally also close write end after dup2) */ not_last ? fd[1] : -1
      );
    }

    // parent process
    children[child_count++] = pid;

    // we don’t need the previous read end anymore, close it
    if (prev_fd_read != -1) close(prev_fd_read);

    if (not_last) {
      // we also don’t need the write end of this pipe in the parent
      close(fd[1]);
      // keep the read end so the next command can use it as stdin
      prev_fd_read = fd[0];
    } else {
      prev_fd_read = -1;
    }
  }

  if (prev_fd_read != -1) close(prev_fd_read);

  // step 4: if this is a foreground job, wait for all child processes to finish
  if (!cmd.background) {
    for (int i = 0; i < child_count; ++i) {
      int status;
      waitpid(children[i], &status, 0);
    }
  }
}


static void child_exec(Pgm *pgm, Command *cmd,
                       int fd_in, int fd_out,
                       int is_first, int is_last,
                       int close_fd1, int close_fd2)
{
    // shared (both run_cmd and run_piped_cmd)
    if (cmd->background)
    {
        // put background jobs in their own process group so they do not die on Ctrl+C
        setpgid(0, 0);
    }
    // child should react normally to ctrl+c
    signal(SIGINT, SIG_DFL); // Enable Ctrl+C on child process

    // run_cmd-specific (single command, no pipes)
    if (fd_in == -1 && is_first && cmd->rstdin)
    {
        // Check if result should be inputted to file (>)
        // first command and user gave an input redirection file
        if (freopen(cmd->rstdin, "r", stdin) == NULL)
        {
            perror_red("freopen rstdin");
            _exit(1);
        }
    }
    if (fd_out == -1 && is_last && cmd->rstdout)
    {
        // Check if result should be outputted to file (<)
        // last command and user gave an output redirection file
        if (freopen(cmd->rstdout, "w", stdout) == NULL)
        {
            perror_red("freopen rstdout");
            _exit(1);
        }
    }

    // run_piped_cmd-specific (pipes)
    if (fd_in != -1)
    {
        // not the first command, so take input from the previous pipe
        if (dup2(fd_in, STDIN_FILENO) < 0)
        {
            perror_red("dup2 stdin");
            _exit(1);
        }
    }
    if (fd_out != -1)
    {
        // not the last command, so send output into the pipe
        if (dup2(fd_out, STDOUT_FILENO) < 0)
        {
            perror_red("dup2 stdout");
            _exit(1);
        }
    }

    // run_piped_cmd cleanup (closing pipe ends not used)
    if (fd_in != -1) close(fd_in);
    if (fd_out != -1) close(fd_out);
    if (close_fd1 != -1 && close_fd1 != fd_in && close_fd1 != fd_out) close(close_fd1);
    if (close_fd2 != -1 && close_fd2 != fd_in && close_fd2 != fd_out && close_fd2 != close_fd1) close(close_fd2);

    // shared tail (both run_cmd and run_piped_cmd)
    // Parse command
    // fprintf(stderr, "\033[32m"); // Set green color

    // finally execute the program
    execvp(pgm->pgmlist[0], pgm->pgmlist);
    // If code reaches this point an error occured as execv should auto exit child process
    perror_red("execvp");
    _exit(1);
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

int split_string_by_pipes(char str[], char *tokens[])
{
  int count = 0;
  char *token = strtok(str, "|");

  while (token != NULL && count < MAX_TOKENS)
  {
    // Remove leading/trailing spaces
    while (*token == ' ')
      token++; // leading space
    char *end = token + strlen(token) - 1;
    while (end > token && *end == ' ')
    {
      *end = '\0';
      end--;
    }
    // Save token into array
    tokens[count++] = strdup(token);
    token = strtok(NULL, "|");
  }
  return count; // Return number of tokens stored
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
