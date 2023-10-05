#include "wsh.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>

// struct proc
// {
//   // job id
//   int job_id;
//   // arg count
//   int argc;
//   // arg array
//   char *argv[256];
//   // proc name
//   char name[256];
//   // running
//   int running;
// };

// // array of all processes
// struct proc processes[256];
// int curr_id = 0;

struct termios shell_tmodes;
pid_t shell_pgid;
int shell_terminal;

/* A process is a single process.  */
typedef struct process
{
  struct process *next; /* next process in pipeline */
  char **argv;          /* for exec */
  pid_t pid;            /* process ID */
  char completed;       /* true if process has completed */
  char stopped;         /* true if process has stopped */
  int status;           /* reported status value */
} process;

/* A job is a pipeline of processes.  */
typedef struct job
{
  struct job *next;          /* next active job */
  char *command;             /* command line, used for messages */
  process *first_process;    /* list of processes in this job */
  pid_t pgid;                /* process group ID */
  char notified;             /* true if user told about stopped job */
  struct termios tmodes;     /* saved terminal modes */
  int stdin, stdout, stderr; /* standard i/o channels */
} job;

/* The active jobs are linked into a list.  This is its head.   */
job *first_job = NULL;

/* Find the active job with the indicated pgid.  */
job *find_job(pid_t pgid)
{
  job *j;

  for (j = first_job; j; j = j->next)
    if (j->pgid == pgid)
      return j;
  return NULL;
}

/* Return true if all processes in the job have stopped or completed.  */
int job_is_stopped(job *j)
{
  process *p;

  for (p = j->first_process; p; p = p->next)
    if (!p->completed && !p->stopped)
      return 0;
  return 1;
}

/* Return true if all processes in the job have completed.  */
int job_is_completed(job *j)
{
  process *p;

  for (p = j->first_process; p; p = p->next)
    if (!p->completed)
      return 0;
  return 1;
}

/////

// built-in commands
void wsh_exit()
{
  exit(0);
}

void wsh_cd(int argc, char *argv[])
{
  argc -= 1;
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
  job *j;
  process *p;

  // iterate over all possible entires in processes array
  // TODO print if not NULL??
  for (j = first_job; j; j = j->next)
  {
    for (p = j->first_process; p; p = p->next)
    {
      printf("%d\n", p->pid);
    }
  }
}

// TODO finish fg
// fg should move a process from the background to the foreground
void wsh_fg(int argc, char *argv[])
{
  argc -= 1;
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
// bg should resume a process in the background - or run any suspended job in the background
void wsh_bg(int argc, char *argv[])
{
  argc -= 1;
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

/////
int mark_process_status(pid_t pid, int status)
{
  job *j;
  process *p;

  if (pid > 0)
  {
    /* Update the record for the process.  */
    for (j = first_job; j; j = j->next)
      for (p = j->first_process; p; p = p->next)
        if (p->pid == pid)
        {
          p->status = status;
          if (WIFSTOPPED(status))
            p->stopped = 1;
          else
          {
            p->completed = 1;
            if (WIFSIGNALED(status))
              fprintf(stderr, "%d: Terminated by signal %d.\n",
                      (int)pid, WTERMSIG(p->status));
          }
          return 0;
        }
    fprintf(stderr, "No child process %d.\n", pid);
    return -1;
  }

  else if (pid == 0 || errno == ECHILD)
    /* No processes ready to report.  */
    return -1;
  else
  {
    /* Other weird errors.  */
    perror("waitpid");
    return -1;
  }
}

void wait_for_job(job *j)
{
  int status;
  pid_t pid;

  do
    pid = waitpid(WAIT_ANY, &status, WUNTRACED);
  while (!mark_process_status(pid, status) && !job_is_stopped(j) && !job_is_completed(j));
}

void put_job_in_background(job *j, int cont)
{
  /* Send the job a continue signal, if necessary.  */
  if (cont)
    if (kill(-j->pgid, SIGCONT) < 0)
      perror("kill (SIGCONT)");
}

void put_job_in_foreground(job *j, int cont)
{
  /* Put the job into the foreground.  */
  tcsetpgrp(shell_terminal, j->pgid);

  /* Send the job a continue signal, if necessary.  */
  if (cont)
  {
    tcsetattr(shell_terminal, TCSADRAIN, &j->tmodes);
    if (kill(-j->pgid, SIGCONT) < 0)
      perror("kill (SIGCONT)");
  }

  /* Wait for it to report.  */
  wait_for_job(j);

  /* Put the shell back in the foreground.  */
  tcsetpgrp(shell_terminal, shell_pgid);

  /* Restore the shell’s terminal modes.  */
  tcgetattr(shell_terminal, &j->tmodes);
  tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
}

void launch_process(process *p, pid_t pgid,
                    int infile, int outfile, int errfile,
                    int foreground)
{
  pid_t pid;
  /* Put the process into the process group and give the process group
      the terminal, if appropriate.
      This has to be done both by the shell and in the individual
      child processes because of potential race conditions.  */
  pid = getpid();
  if (pgid == 0)
    pgid = pid;
  setpgid(pid, pgid);
  if (foreground)
    tcsetpgrp(shell_terminal, pgid);

  /* Set the handling for job control signals back to the default.  */
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTSTP, SIG_DFL);
  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
  signal(SIGCHLD, SIG_DFL);

  /* Set the standard input/output channels of the new process.  */
  if (infile != STDIN_FILENO)
  {
    dup2(infile, STDIN_FILENO);
    close(infile);
  }
  if (outfile != STDOUT_FILENO)
  {
    dup2(outfile, STDOUT_FILENO);
    close(outfile);
  }
  if (errfile != STDERR_FILENO)
  {
    dup2(errfile, STDERR_FILENO);
    close(errfile);
  }

  /* Exec the new process.  Make sure we exit.  */
  execvp(p->argv[0], p->argv);
  perror("execvp");
  exit(1);
}

void launch_job(job *j, int foreground)
{
  process *p;
  pid_t pid;
  int mypipe[2], infile, outfile;

  infile = j->stdin;
  for (p = j->first_process; p; p = p->next)
  {
    /* Set up pipes, if necessary.  */
    if (p->next)
    {
      outfile = mypipe[1];
    }
    else
      outfile = j->stdout;

    /* Fork the child processes.  */
    pid = fork();
    if (pid == 0)
      /* This is the child process.  */
      launch_process(p, j->pgid, infile,
                     outfile, j->stderr, foreground);
    else if (pid < 0)
    {
      /* The fork failed.  */
      perror("fork");
      exit(1);
    }
    else
    {
      /* This is the parent process.  */
      p->pid = pid;
      if (!j->pgid)
        j->pgid = pid;
      setpgid(pid, j->pgid);
    }

    /* Clean up after pipes.  */
    if (infile != j->stdin)
      close(infile);
    if (outfile != j->stdout)
      close(outfile);
    infile = mypipe[0];
  }

  // format_job_info (j, "launched");

  wait_for_job(j);
  if (foreground)
  {
    put_job_in_foreground(j, 0);
  }
  else
  {
    put_job_in_background(j, 0);
  }
}

// // TODO implement special function for multiple commands
// void run_fg_job()
// {
// }

// void run_bg_job()
// {
// }

// // helper functions
// void run_fg_proc(char *file, int argc, char *argv[])
// {
//   int pid = fork();
//   if (pid < 0)
//   {
//     printf("ERROR: error running %s as a child process.\n", file);
//     wsh_exit();
//   }
//   // child
//   else if (pid == 0)
//   {
//     // reset signal handlers to default
//     signal(SIGINT, SIG_DFL);
//     signal(SIGTSTP, SIG_DFL);

//     // Put the process into the process group and give the process group the terminal, if appropriate.
//     pid_t pgid = pid;
//     setpgid(pid, pgid);
//     tcsetpgrp(shell_terminal, pgid);

//     // execute job
//     if (execvp(file, argv) == -1)
//     {
//       printf("ERROR: execvp failed to run %s\n", file);
//     };
//   }
//   // parent
//   else if (pid > 0)
//   {

//     // TODO admin process groups?
//     // TODO set file descriptors accordingly... dup2() before forking
//     wait(0);
//     // TODO handle SIGCHLD -> handler to remove child from process array???
//     printf("FROM PARENT: name: %s, id: %d\n", processes[curr_id].name, processes[curr_id].job_id);
//   }
// }

// void run_bg_proc(char *file, int argc, char *argv[])
// {
//   int pid = fork();

//   if (pid < 0)
//   {
//     printf("ERROR: error running %s as a child process.\n", file);
//     wsh_exit();
//   }

//   // populate process struct in processes array
//   // struct proc curr_proc;

//   // strcpy(curr_proc.name, file);
//   // curr_proc.argc = argc;
//   // for (int i = 0; i < argc; i++)
//   // {
//   //   curr_proc.argv[i] = argv[i];
//   // }
//   // curr_proc.job_id = curr_id + 1;

//   // processes[curr_id] = curr_proc;

//   // curr_id += 1;

//   // execute job
//   if (execvp(file, argv) == -1)
//   {
//     printf("ERROR: execvp failed to run %s\n", file);
//   };
// }

// run function for interactive mode
int runi()
{
  // ignore job control signals
  signal(SIGINT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  shell_terminal = STDIN_FILENO;

  // Put ourselves in our own process group
  shell_pgid = getpid();
  if (setpgid(shell_pgid, shell_pgid) < 0)
  {
    perror("Couldn't put the shell in its own process group");
    exit(1);
  }
  // Grab control of the terminal
  tcsetpgrp(shell_terminal, shell_pgid);
  // Save default terminal attributes for shell.
  tcgetattr(shell_terminal, &shell_tmodes);

  while (true)
  {
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

    int num_pipes = 0;
    int bg = 0;

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
      if (i != cmd_argc - 1 && strcmp(cmd_seg, "|") == 0)
      {
        num_pipes += 1;
      }
      else if (i != cmd_argc - 1 && strcmp(cmd_seg, "&") == 0)
      {
        bg = 1;
      }
      cmd_argv[i] = cmd_seg;
      cmd_seg = strtok(NULL, " ");
    }
    // NULL terminate
    cmd_argv[cmd_argc] = NULL;

    if (cmd_argc - 1 > 0)
    {
      // TODO handle pipe process
      if (bg)
      {
        printf(">>>>bg<<<<%d\n", cmd_argc);
        char *argv[256];
        for (int i; i < cmd_argc - 1; i++)
        {
          argv[i] = cmd_argv[i];
        }
        argv[cmd_argc - 1] = NULL;

        process new_bg_proc = {.next = NULL, .argv = argv, .pid = getpid()};
        job new_bg_job = {.next = NULL, .first_process = &new_bg_proc, .pgid = getpgid(getpid()), .stdin = 0, .stdout = 1, .stderr = 2};
        launch_job(&new_bg_job, 1);
      }
      else
      {
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
          // // run process in foreground
          // run_fg_proc(cmd_argv[0], cmd_argc, cmd_argv);
          process new_proc = {.next = NULL, .argv = cmd_argv, .pid = getpid()};
          job new_job = {.next = NULL, .first_process = &new_proc, .pgid = getpgid(getpid()), .stdin = 0, .stdout = 1, .stderr = 2};
          launch_job(&new_job, 1);
        }
      }
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
