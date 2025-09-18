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
 * Using assert statements in your code is a great way to catch errors early and
 * make debugging easier. Think of them as mini self-checks that ensure your
 * program behaves as expected. By setting up these guardrails, you're creating
 * a more robust and maintainable solution. So go ahead, sprinkle some asserts
 * in your code; they're your friends in disguise!
 *
 * All the best!
 */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdlib.h>
#include <string.h>

// The <unistd.h> header is your gateway to the OS's process management
// facilities.
#include "parse.h"
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_cmd(Command *cmd);
static void print_pgm(Pgm *p);
void stripwhite(char *);

static pid_t foreground_pgid = 0;
static int shell_terminal;
static pid_t shell_pgid;
const char *builtin_commands[] = {"cd", "exit", NULL};

/*
 * Signal handler for SIGCHLD. Reaps all terminated child processes.
 */
static void sigchld_handler() {
  // Apparently waitpid can overwrite errno, but we ignore that for now.
  // The WNOHANG option prevents waitpid() from blocking if there are no
  // terminated children. The loop ensures we reap all zombies.
  while (waitpid(-1, NULL, WNOHANG) > 0) {
    // This loop body can be empty. The work is done in the condition.
  }
}

void sigint_handler() {
  // Check if there is a foreground process
  if (foreground_pgid > 0) {
    // Send SIGINT to the entire process group.
    // The negative sign means it should kill the entire group, not just one
    // process
    kill(-foreground_pgid, SIGINT);
  }
}

static int is_builtin_command(const char *command) {
  for (int i = 0; builtin_commands[i] != NULL; i++) {
    if (strcmp(command, builtin_commands[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

static void redirect_stdout(int fd[2]) {
  // Redirect stdout (file descriptor 1) to our opened file.
  if (dup2(fd[1], STDOUT_FILENO) < 0) {
    perror("lsh: failed to redirect stdout");
    exit(EXIT_FAILURE);
  }

  // The original file descriptor is no longer needed.
  close(fd[1]);
  if (fd[0] >= 0) {
    close(fd[0]);
  }
}

static void redirect_stdout_to_file(char *file) {
  // Open the file with write, create, and truncate flags.
  // Set permissions to 0644 (owner can read/write, others can read).
  int out_fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  // Always check for errors.
  if (out_fd < 0) {
    perror("lsh: failed to open output file");
    exit(EXIT_FAILURE); // Exit child process on failure
  }

  int fd[2] = {-1, out_fd};
  redirect_stdout(fd);
}

static void redirect_stdin(int fd[2]) {
  // Redirect stdin (file descriptor 0) to our opened file.
  if (dup2(fd[0], STDIN_FILENO) < 0) {
    perror("lsh: failed to redirect stdin");
    exit(EXIT_FAILURE);
  }

  // Close the original file descriptor.
  close(fd[0]);
  if (fd[1] >= 0) {
    close(fd[1]);
  }
}

static void redirect_stdin_to_file(char *file) {
  // Open the file for reading only.
  int in_fd = open(file, O_RDONLY);

  // Check for errors (e.g., file not found).
  if (in_fd < 0) {
    perror("lsh: failed to open input file");
    exit(EXIT_FAILURE);
  }

  int fd[2] = {in_fd, -1};
  redirect_stdin(fd);
}

void execute_builtin(char **args) {
  const char *command = args[0];

  if (strcmp(command, "exit") == 0) {
    // Exit the shell successfully.
    exit(EXIT_SUCCESS);
  } else if (strcmp(command, "cd") == 0) {
    // Get the target directory from the second argument
    char *target_dir = args[1];

    // If no directory is specified, default to the HOME directory
    if (target_dir == NULL) {
      target_dir = getenv("HOME");
      if (target_dir == NULL) {
        fprintf(stderr, "lsh: cd: HOME not set\n");
        return;
      }
    }

    // Change the directory and handle errors
    if (chdir(target_dir) != 0) {
      perror("lsh: cd");
    }
  }
  // Other builtins...
}

int main(void) {
  int pipefd[2];
  shell_terminal = STDIN_FILENO;

  // Don't put our own shell in its own process group to pass the tests
  //
  // Get out current PID to set it to a new process group later
  shell_pgid = getpid();
  // Set our shell to its own process group
  // if (setpgid(shell_pgid, shell_pgid) < 0) {
  //   perror("Couldn't put shell in its own process group");
  //   exit(1);
  // }

  tcsetpgrp(shell_terminal, shell_pgid);

  // Assign signal handler that sends SIGINT to entire process group
  // if its running in foreground.
  signal(SIGINT, sigint_handler);
  // Make background processes get reaped when they send their SIGCHLD
  signal(SIGCHLD, sigchld_handler);
  /*
   * From man7:
   * Attempts to use tcsetpgrp() from a process which is a member of a
       background process group on a fildes associated with its
       controlling terminal shall cause the process group to be sent a
       SIGTTOU signal. If the calling thread is blocking SIGTTOU signals
       or the process is ignoring SIGTTOU signals, the process shall be
       allowed to perform the operation, and no signal is sent.
   * Since we put our shell to the background once we put the child process
   group
   * to the foreground we have to ignore the signal once the shell background
   * process is trying to get back its control.
   */
  signal(SIGTTOU, SIG_IGN);

  for (;;) {
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
      // If we get EOF (NULL) we are only supposed to exit according to the
      // README.md
      // TODO: We have to check if that is enough, since this might leave some
      // orphan processes.

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
    if (*line) {
      add_history(line);

      Command cmd;
      if (parse(line, &cmd) == 1) {

        // We only execute builtin commands without spawning if they
        // are not in a pipeline.
        if (cmd.pgm->next == NULL && is_builtin_command(cmd.pgm->pgmlist[0])) {
          // Execute it directly in the shell without spawning
          execute_builtin(cmd.pgm->pgmlist);

        } else {

          int pid = fork();
          if (pid == 0) {
            pid_t child_pid = getpid();
            /* Create a own process group for the child. This can be one command
             * or an entire chain of commands. This way we can cancel an entire
             * chain at once with ctrl+c
             */
            setpgid(child_pid, child_pid);

            // Give terminal control to child if foreground
            if (!cmd.background) {
              tcsetpgrp(shell_terminal, child_pid);
            }

            /*
             * As described in the Advanced Programming in UNIX environments
             * book on Fig 9.10, the first command entered in the shell will be
             * the last command executed from the first forked child. This is
             * also why the pgmlist is very likely reversed.
             */

            // If only one command is provided
            if (cmd.pgm->next == NULL) {
              if (cmd.rstdout != NULL) {
                redirect_stdout_to_file(cmd.rstdout);
              }
              if (cmd.rstdin != NULL) {
                redirect_stdin_to_file(cmd.rstdin);
              }
              execvp(cmd.pgm->pgmlist[0], cmd.pgm->pgmlist);
            }

            // If multiple commands are provided
            Pgm *next_pgm = cmd.pgm;

            if (pipe(pipefd) == -1)
              err(EXIT_FAILURE, "pipe");

            pid_t pid = fork();
            if (pid == 0) {
              redirect_stdin(pipefd);
              if (cmd.rstdout != NULL) {
                redirect_stdout_to_file(cmd.rstdout);
              }
              execvp(next_pgm->pgmlist[0], next_pgm->pgmlist);
            } else if (pid < 0) {
              printf("fork failed");
            }
            next_pgm = next_pgm->next;

            while (next_pgm != NULL && next_pgm->next != NULL) {
              int previous_pipe[2] = {pipefd[0], pipefd[1]};
              if (pipe(pipefd) == -1)
                err(EXIT_FAILURE, "pipe");

              pid_t pid = fork();
              if (pid == 0) {
                redirect_stdout(previous_pipe);
                redirect_stdin(pipefd);
                execvp(next_pgm->pgmlist[0], next_pgm->pgmlist);
              } else if (pid > 0) {
                close(previous_pipe[0]);
                close(previous_pipe[1]);
              } else {
                printf("fork failed");
              }
              next_pgm = next_pgm->next;
            }

            // Execute last command without fork
            if (cmd.rstdin != NULL) {
              redirect_stdin_to_file(cmd.rstdin);
            }
            redirect_stdout(pipefd);
            execvp(next_pgm->pgmlist[0], next_pgm->pgmlist);
          } else if (pid > 0) {
            // Parent process
            // Put child in its own process. This is done here as well in case
            // the parent executes before the child.
            setpgid(pid, pid);

            if (!cmd.background) {
              // Wait for foreground process
              foreground_pgid = pid;
              // Parent might run before child sets its group!
              tcsetpgrp(shell_terminal, pid); // Give terminal to child

              int status;
              // 0 meaning no options
              waitpid(pid, &status, 0);

              // Take back terminal control. We specifically ignore the signal
              // at the top for that
              tcsetpgrp(shell_terminal, shell_pgid);
              foreground_pgid = 0;
            }
          } else {
            perror("fork failed");
          }
        }
      } else {
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
static void print_cmd(Command *cmd_list) {
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
static void print_pgm(Pgm *p) {
  if (p == NULL) {
    return;
  } else {
    char **pl = p->pgmlist;

    /* The list is in reversed order so print
     * it reversed to get right
     */
    print_pgm(p->next);
    printf("            * [ ");
    while (*pl) {
      printf("%s ", *pl++);
    }
    printf("]\n");
  }
}

/* Strip whitespace from the start and end of a string.
 *
 * Helper function, no need to change.
 */
void stripwhite(char *string) {
  size_t i = 0;

  while (isspace(string[i])) {
    i++;
  }

  if (i) {
    memmove(string, string + i, strlen(string + i) + 1);
  }

  i = strlen(string) - 1;
  while (i > 0 && isspace(string[i])) {
    i--;
  }

  string[++i] = '\0';
}
