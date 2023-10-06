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

typedef struct process
{
    char *name;           /* name of process */
    struct process *next; /* next process in pipeline */
    char **argv;          /* for exec */
    int argc;
    pid_t pid;                 /* process ID */
    char *status;              /* reported status value */
    int stdin, stdout, stderr; /* standard i/o channels */
} process;

typedef struct job
{
    int job_id;             /* job id of job */
    process *first_process; /* list of processes in this job */
    pid_t pgid;             /* process group ID */
    int foreground;         /* job running in foreground (1) or background (0) */
    struct termios tmodes;  /* saved terminal modes */
} job;

// array of all processes
struct job *jobs[256];
int curr_id = 0;

struct termios shell_tmodes;
pid_t shell_pgid;
int shell_terminal;

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
    }
    else
    {
        // change directories
        if (chdir(argv[1]) != 0)
        {
            printf("Error: chdir to %s failed.\n", argv[1]);
        }
    }
}

// <id>: <program name> <arg1> <arg2> … <argN> [&]
void wsh_jobs()
{
    process *p;

    for (int i = 0; i < 256; i++)
    {
        if (jobs[i] != NULL && jobs[i]->foreground == 0)
        {
            printf("%d: ", jobs[i]->job_id);
            for (p = jobs[i]->first_process; p; p = p->next)
            {
                printf("%s ", p->name);
                for (int i = 1; i < p->argc - 1; i++)
                {
                    printf("%s ", p->argv[i]);
                }

                //! remove
                printf("\t(%s)", p->status);
                // TODO print pipe somehow
            }
            // TODO print & for bg jobs
            printf("\n");
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

void sigchld_handler(int signum)
{
    pid_t pid;
    int   status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        printf("child ended: %d\n", pid);
    }
}

// TODO implement special functions for piped processes
void run_fg_pipe(job *j)
{
}

void run_bg_pipe(job *j)
{
}

// helper functions
void run_fg_job(job *j)
{
    process *p = j->first_process;

    int pid = fork();
    if (pid < 0)
    {
        printf("ERROR: error running %s as a child process.\n", p->argv[0]);
        wsh_exit();
    }
    // child
    else if (pid == 0)
    {
        pid_t pgid = j->pgid;

        pid_t ipid;
        ipid = getpid ();
        if (pgid == 0) pgid = ipid;
        setpgid (ipid, pgid);
        tcsetpgrp (shell_terminal, pgid);
        
        // reset signal handlers to default
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        // // IN BELOW REPLACE INFILE/OUTFILE WITH P->STDIN/P->STDOUT
        // if (infile != STDIN_FILENO)
        //     {
        //     dup2 (infile, STDIN_FILENO);
        //     close (infile);
        //     }
        // if (outfile != STDOUT_FILENO)
        //     {
        //     dup2 (outfile, STDOUT_FILENO);
        //     close (outfile);
        //     }
        // if (errfile != STDERR_FILENO)
        //     {
        //     dup2 (errfile, STDERR_FILENO);
        //     close (errfile);
        // }

        // execute job
        if (execvp(p->argv[0], p->argv) == -1)
        {
            printf("ERROR: execvp failed to run %s\n", p->argv[0]);
        };

        // Put the job into the foreground.
        tcsetpgrp(shell_terminal, j->pgid);

        exit(1);
    }
    // parent
    else if (pid > 0)
    {
        p->pid = pid;
        if (!j->pgid)
        {
            j->pgid = pid;
        }
        setpgid(pid, j->pgid);


        /* Put the job into the foreground.  */
        tcsetpgrp (shell_terminal, j->pgid);

        // TODO admin process groups?
        // TODO set file descriptors accordingly... dup2() before forking
        int status;
        pid = waitpid(-1, &status, WNOHANG);

        /* Put the shell back in the foreground.  */
        tcsetpgrp (shell_terminal, shell_pgid);

        /* Restore the shell’s terminal modes.  */
        tcgetattr (shell_terminal, &j->tmodes);
        tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);

        // TODO handle SIGCHLD -> handler to remove child from process array???
    }
}

void run_bg_job(job *j)
{
    process *p = j->first_process;

    int pid = fork();

    if (pid < 0)
    {
        printf("ERROR: error running %s as a child process.\n", p->argv[0]);
        wsh_exit();
    }
    else if (pid == 0)
    {
        pid_t pgid = j->pgid;
        
        pid_t ipid;
        ipid = getpid ();
        if (pgid == 0) pgid = ipid;
        setpgid (ipid, pgid);
        tcsetpgrp (shell_terminal, pgid);

        // // IN BELOW REPLACE INFILE/OUTFILE WITH P->STDIN/P->STDOUT
        // if (infile != STDIN_FILENO)
        //     {
        //     dup2 (infile, STDIN_FILENO);
        //     close (infile);
        //     }
        // if (outfile != STDOUT_FILENO)
        //     {
        //     dup2 (outfile, STDOUT_FILENO);
        //     close (outfile);
        //     }
        // if (errfile != STDERR_FILENO)
        //     {
        //     dup2 (errfile, STDERR_FILENO);
        //     close (errfile);
        // }

        // continue to ignore signals
        signal(SIGINT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        // TODO handle child end singal
        signal(SIGCHLD, SIG_IGN);

        // execute job
        if (execvp(p->argv[0], p->argv) == -1)
        {
            printf("ERROR: execvp failed to run %s\n", p->argv[0]);
        };

        exit(1);
    }
    // parent
    else if (pid > 0)
    {
        p->pid = pid;
        if (!j->pgid)
        {
            j->pgid = pid;
        }
        setpgid(pid, j->pgid);

        // TODO admin process groups?
        // TODO set file descriptors accordingly... dup2() before forking

        // /* Put the shell back in the foreground.  */
        // tcsetpgrp (shell_terminal, shell_pgid);

        // /* Restore the shell’s terminal modes.  */
        // tcgetattr (shell_terminal, &j->tmodes);
        // tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);

        // TODO handle SIGCHLD -> handler to remove child from process array???
    }
}

void populate_process_struct(process *p, char *name, process *next, int argc, char *argv[], pid_t pid, char *status, int stdin, int stdout, int stderr)
{
    // allocate and set name
    p->name = malloc(256);
    strcpy(p->name, name);

    // allocate and set next pointer (for piping)
    p->next = (struct process *)malloc(sizeof(struct process));
    p->next = next;

    // allocate and set cmd args (for exec)
    p->argv = malloc(sizeof(*argv) * argc);
    for (int i = 0; i < argc - 1; i++)
    {
        p->argv[i] = strdup(argv[i]);
    }
    p->argv[argc] = NULL;

    p->argc = argc;

    // set pid
    p->pid = pid;

    // allocate and set status
    p->status = malloc(256);
    strcpy(p->status, status);

    // set fds
    p->stdin = 0;
    p->stdout = 1;
    p->stderr = 2;
}

void populate_job_struct(job *j, int job_id, process *fp, pid_t pgid, int foreground)
{
    // set job id
    j->job_id = job_id;

    // allocate and set first process in job
    j->first_process = (struct process *)malloc(sizeof(struct process));
    j->first_process = fp;

    // set pgid
    j->pgid = pgid;

    // set foreground bool
    j->foreground = foreground;
}

// run function for interactive mode
int runi()
{
    // ignore job control signals
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);
    // signal(SIGCHLD, SIG_IGN);

    // shell_terminal = STDIN_FILENO;

    // Put ourselves in our own process group
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0)
    {
        perror("Couldn't put the shell in its own process group");
        exit(1);
    }

    while (true)
    {
        // Grab control of the terminal
        tcsetpgrp(shell_terminal, shell_pgid);
        // Save default terminal attributes for shell.
        tcgetattr(shell_terminal, &shell_tmodes);

        printf("wsh> ");

        // collect user cmd
        char cmd[256];
        char *line = fgets(cmd, sizeof(cmd), stdin);

        // check if EOF is reached/input
        if (line == NULL)
        {
            printf("EOF\n");
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
            // TODO handle piped processes
            if (bg)
            {
                cmd_argv[cmd_argc - 2] = NULL;
                cmd_argc -= 1;

                process *p = (struct process *)malloc(sizeof(struct process));
                job *j = (struct job *)malloc(sizeof(struct job));

                char *status = "running";

                populate_process_struct(p, cmd_argv[0], NULL, cmd_argc, cmd_argv, 0, status, 0, 1, 2);
                populate_job_struct(j, curr_id + 1, p, 0, 0);

                jobs[curr_id] = j;
                curr_id += 1;

                run_bg_job(j);
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
                // run child process/job
                else
                {
                    // run process in a job in the foreground
                    process *p = (struct process *)malloc(sizeof(struct process));
                    job *j = (struct job *)malloc(sizeof(struct job));

                    char *status = "running";

                    populate_process_struct(p, cmd_argv[0], NULL, cmd_argc, cmd_argv, 0, status, 0, 1, 2);
                    populate_job_struct(j, curr_id + 1, p, 0, 1);

                    jobs[curr_id] = j;
                    curr_id = curr_id + 1;

                    run_fg_job(j);
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
