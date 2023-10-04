#include "wsh.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

struct proc
{
  // job id
  int job_id;
  // arg count
  int argc;
  // arg array
  char *argv[256];
  // foreground bool
  int fg;
  // proc name
  char name[256];
};

// array of all processes
struct proc processes[256];

int curr_id = 0;

// built-in commands
void wsh_exit()
{
  exit(0);
}

void wsh_cd(int argc, char *argv[])
{
  if (argc != 2)
  {
    printf("USAGE: cd dir\n");
    // wsh_exit();
  }
  else
  {
    // change directories
    if (chdir(argv[1]) != 0)
    {
      printf("Error: chdir to %s failed.\n", argv[1]);
      // wsh_exit();
    }
  }
}

// <id>: <program name> <arg1> <arg2> … <argN> [&]
void wsh_jobs()
{
  // iterate over all possible entires in processes array
  for (int i = 0; i < 256; i++)
  {
    printf("Searching processes array!\n");

    // end when the entry is null
    if (strcmp(processes[i].name, "") == 0)
    {
      printf("Found null entry\n");
      break;
    }

    // only print background jobs
    if (processes[i].fg == 0)
    {
      printf("%d: ", processes[i].job_id);
      for (int j = 0; j < processes[i].argc; j++)
      {
        printf("%s ", processes[i].argv[j]);
      }
      printf("\n");
    }
  }
}

// TODO finish fg
void wsh_fg(int argc, char *argv[])
{
  // id was provided
  if (argc == 2)
  {
  }
  // use most recent id
  else if (argc == 1)
  {
  }
  else
  {
    printf("USAGE: fg [job_id]\n");
    // wsh_exit();
  }
}

// TODO finish bg
void wsh_bg(int argc, char *argv[])
{
  // id was provided
  if (argc == 2)
  {
  }
  // use most recent id
  else if (argc == 1)
  {
  }
  else
  {
    printf("USAGE: fg [job_id]\n");
    // wsh_exit();
  }
}

// helper functions
void run_fg_proc(char *file, int argc, char *argv[])
{
  int pid = fork();
  if (pid < 0)
  {
    printf("ERROR: error running %s as a child process.\n", file);
    wsh_exit();
  }
  // child
  else if (pid == 0)
  {
    struct proc curr_proc;

    // populate process struct in processes array
    strcpy(curr_proc.name, file);
    curr_proc.argc = argc;
    for (int i = 0; i < argc; i++)
    {
      curr_proc.argv[i] = argv[i];
    }
    curr_proc.fg = 0;
    curr_proc.job_id = curr_id + 1;

    processes[curr_id] = curr_proc;

    curr_id += 1;

    // execute job
    execvp(file, argv);
  }
  // parent
  else if (pid > 0)
  {
    wait(0);
    printf("FROM PARENT: name: %s, id: %d, fg?:%d\n", processes[curr_id].name, processes[curr_id].job_id, processes[curr_id].fg);
  }
}

void run_bg_proc(char *file, int argc, char *argv[])
{

  printf("running: %s\n", file);
  for (int i = 0; i < argc; i++)
  {
    printf("arg%d: %s\n", i, argv[i]);
  }

  int pid = fork();

  if (pid < 0)
  {
    printf("ERROR: error running %s as a child process.\n", file);
    wsh_exit();
  }

  // populate process struct in processes array
  struct proc curr_proc;

  strcpy(curr_proc.name, file);
  curr_proc.argc = argc;
  for (int i = 0; i < argc; i++)
  {
    curr_proc.argv[i] = argv[i];
  }
  curr_proc.fg = 0;
  curr_proc.job_id = curr_id + 1;

  processes[curr_id] = curr_proc;

  curr_id += 1;

  // execute job
  execvp(file, argv);
  exit(0);
}

// run function for interactive mode
int runi()
{
  while (true)
  {
    // ignore CTRL-C and CTRL-Z
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    printf("wsh> ");

    // collect user cmd
    char cmd[256];
    char *line = fgets(cmd, sizeof(cmd), stdin);

    // check if EOF is reached/input
    if (line == NULL)
    {
      wsh_exit();
    }

    // remove newline
    if (cmd[strlen(cmd) - 1] == '\n')
    {
      cmd[strlen(cmd) - 1] = '\0';
    }

    // TODO: check for pipe char and & char
    // TODO: I think we have to be able to handle both, so maybe create booleans to then handle these cases after command has been fully parsed

    // parse command
    char tmp_cmd[256];
    strcpy(tmp_cmd, cmd);
    int cmd_argc = 0;

    char *cmd_seg = strtok(tmp_cmd, " ");
    while (cmd_seg != NULL)
    {
      cmd_argc += 1;
      cmd_seg = strtok(NULL, " ");
    }
    // add room for NULL termination
    cmd_argc += 1;

    strcpy(tmp_cmd, cmd);
    char *cmd_argv[cmd_argc];
    cmd_seg = strtok(tmp_cmd, " ");
    for (int i = 0; i < cmd_argc; i++)
    {
      cmd_argv[i] = cmd_seg;
      cmd_seg = strtok(NULL, " ");
    }
    // NULL terminate
    cmd_argv[cmd_argc] = NULL;

    // exit
    if (strcmp(cmd_argv[0], "exit") == 0)
    {
      wsh_exit();
    }
    // cd
    else if (strcmp(cmd_argv[0], "cd") == 0)
    {
      wsh_cd(cmd_argc, cmd_argv);
    }
    // jobs
    else if (strcmp(cmd_argv[0], "jobs") == 0)
    {
      wsh_jobs();
    }
    // fg
    else if (strcmp(cmd_argv[0], "fg") == 0)
    {
      wsh_fg(cmd_argc, cmd_argv);
    }
    // bg
    else if (strcmp(cmd_argv[0], "bg") == 0)
    {
      wsh_bg(cmd_argc, cmd_argv);
    }
    // search path
    else
    {
      // run process in foreground
      run_fg_proc(cmd_argv[0], cmd_argc, cmd_argv);
    }
  }
  return 0;
}

// TODO: run function for batch mode
int runb(char *batch_file)
{
  // open batch file and iterate over it
  return 0;
}

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2)
  {
    printf("Usage: ./wsh [batch_file]\n");
    exit(1);
  }

  // interactive mode
  if (argc == 1)
  {
    runi();
  }
  // batch mode
  else if (argc == 2)
  {
    char *file_name = argv[1];
    runb(file_name);
  }

  return 0;
}
