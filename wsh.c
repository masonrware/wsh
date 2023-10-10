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
    int argc;             /* arg count */
    pid_t pid;            /* process ID */
    char stopped;         /* stopped indicator */
    int status;           /* status indicator */
    char completed;       /* completed indicator */
    int dead;             /* dead indicator */
} process;

typedef struct job
{
    struct job *next;          /* next active job */
    char *command;             /* command line, used for messages */
    process *first_process;    /* list of processes in this job */
    pid_t pgid;                /* process group ID */
    char notified;             /* true if user told about stopped job */
    struct termios tmodes;     /* saved terminal modes */
    int foreground;            /* foreground job indicator*/
    int stdin, stdout, stderr; /* standard i/o channels */
    int job_id;                /* job id */
    int dead;                  /* dead indicator */
    int piped;                 /* piped job indicator */
} job;

// array of all jobs
struct job *jobs[256];
int curr_id = 0;

struct termios shell_tmodes;
pid_t shell_pgid;
int shell_terminal;

/*
 * JOB INFORMATION FUNCTIONS
 */

/// @brief Return true if all processes in the job have stopped or completed.
/// @param j job struct pointer
/// @return exit code
int job_is_stopped(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        if (!p->completed && !p->stopped)
            return 0;
    return 1;
}

/// @brief Return true if all processes in the job have completed.
/// @param j job struct pointer
/// @return exit code
int job_is_completed(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        if (!p->completed)
            return 0;
    return 1;
}

/// @brief Given a process id and a status code, update the indicators of the process
/// @param pid the pid of a process
/// @param status the status code of a process
/// @return exit code
int mark_process_status(pid_t pid, int status)
{
    process *p;

    if (pid > 0)
    {
        /* Update the record for the process.  */
        for (int i = 0; i < 256; i++)
        {
            if (jobs[i] != NULL)
            {
                for (p = jobs[i]->first_process; p; p = p->next)
                {
                    if (p->pid == pid)
                    {
                        p->status = status;
                        if (WIFSTOPPED(status))
                            p->stopped = 1;
                        else
                        {
                            p->completed = 1;
                            // if (WIFSIGNALED(status))
                            //     fprintf(stderr, "%d: Terminated by signal %d.\n",
                            //             (int)pid, WTERMSIG(p->status));
                        }
                        return 0;
                    }
                }
            }
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

/// @brief Interrupt system to wait for a job to finish
/// @param j job struct pointer
void wait_for_job(job *j)
{
    int status;
    pid_t pid;

    // handle piped jobs and regular jobs differently
    if (j->piped)
    {
        do
        {
            // wait for any process to finish
            pid = waitpid(WAIT_ANY, &status, WUNTRACED);
        } while (!mark_process_status(pid, status) && !job_is_stopped(j) && !job_is_completed(j));
    }
    else
    {
        do
        {
            // wait for all processes in this job's process group to finish
            pid = waitpid(j->pgid, &status, WUNTRACED);
        } while (!mark_process_status(pid, status) && !job_is_stopped(j) && !job_is_completed(j));
    }

    j->dead = 1;
}

/// @brief Move a running job to the foreground
/// @param j job struct pointer
/// @param cont continuation boolean (unused in current implementation)
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

/// @brief Move a running job to the background (do nothing as all processes are started as background processes)
/// @param j job struct pointer
/// @param cont continuation boolean (unused in current implementation)
void put_job_in_background(job *j, int cont)
{
    /* Send the job a continue signal, if necessary.  */
    if (cont)
        if (kill(-j->pgid, SIGCONT) < 0)
            perror("kill (SIGCONT)");
}

/*
 * HELPER FUNCTIONS/ALGORITHMS
 */

/// @brief Handler to process SIGCHLD
/// @param signum number of signal
void sigchld_handler(int signum)
{
    pid_t pid;
    int status;

    // wait for pid of ended process and do not interrupt the shell
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        // iterate over all jobs
        for (int i = 0; i < 256; i++)
        {
            // if the index of the jobs array is populated and the job is alive
            if (jobs[i] != NULL && (jobs[i]->dead == 0))
            {
                int all_dead = 1;
                // iterate over all processes of the job
                for (process *p = jobs[i]->first_process; p; p = p->next)
                {
                    // if we have a pid match, mark the process dead
                    if (p->pid == pid)
                    {
                        p->dead = 1;
                    }
                    if (p->dead == 0)
                    {
                        all_dead = 0;
                    }
                }
                // if all processes in the job are dead, mark the job dead
                if (all_dead)
                {
                    jobs[i]->dead = 1;
                }
            }
        }
    }
}

/// @brief Algorithm to return the smallest available id
/// @return smallest available job id
int smallest_available_id()
{
    int smallest_available = 1;
    int all_null = 1;

    // iterate over all the jobs
    for (int i = 0; i < 256; i++)
    {
        // if an index in the jobs array is populated, if the job is alive
        // if the job's id is not smaller than the tracker
        if (jobs[i] != NULL && (jobs[i]->dead == 0) && !(jobs[i]->job_id < smallest_available))
        {
            // found a non-null
            if (all_null)
                all_null = 0;
            // if the id is equal, increment and continue search
            if (jobs[i]->job_id == smallest_available)
            {
                smallest_available += 1;
            }
            // if there is no equal, it is the smallest
            else
            {
                return smallest_available;
            }
        }
    }
    // all entries are null -> return 1
    if (all_null)
    {
        return 1;
    }

    return smallest_available;
}

/// @brief Algorithm to find the largest id currently in use
/// @return the largest unavailable job id
int get_largest_id()
{
    int largest_id = 0;
    int all_null = 1;

    // iterate over all jobs
    for (int i = 0; i < 256; i++)
    {
        // if the index of the jobs array is populated and the job is alive
        if (jobs[i] != NULL && (jobs[i]->dead == 0))
        {
            // found a non-null
            if (all_null)
                all_null = 0;
            // if the id is greater than the tracker, set the tracker equal to the job id
            if (jobs[i]->job_id > largest_id)
            {
                largest_id = jobs[i]->job_id;
            }
        }
    }
    // all entries are null -> return 1
    if (all_null)
    {
        return 1;
    }

    return largest_id;
}

/// @brief Function to reverse an array in place
/// @param arr char pointer array to be reversed
/// @param n the size of the char pointer array
/// @return the size of the char pointer array
int reverse_array(char *arr[], int n)
{
    char *temp;
    for (int i = 0; i < n / 2; i++)
    {
        temp = arr[i];
        arr[i] = arr[n - i - 1];
        arr[n - i - 1] = temp;
    }
    arr[n] = NULL;
    return n;
}

/*
 * BUILT IN COMMANDS
 */

/// @brief command to exit the program
void wsh_exit()
{
    exit(0);
}

/// @brief command to implement cd  (changing directories)
/// @param argc the argument count
/// @param argv the argument vector
void wsh_cd(int argc, char *argv[])
{
    // decrement argc to get rid of null termination
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

/// @brief command to display all in progress background jobs (in order by job id) in the following format:
/// <id>: <program name> <arg1> <arg2> … <argN> [&]
void wsh_jobs()
{
    process *p;

    int largest_id = get_largest_id();
    int local_job_id = 1;

    // continue to iterate over all jobs until we have printed the largest id
    while (local_job_id <= largest_id)
    {
        // iterate over all jobs
        for (int i = 0; i < 256; i++)
        {
            // if the index of the jobs array is populated and the job is alive
            // if the job is a background job
            if (jobs[i] != NULL && (jobs[i]->dead == 0) && jobs[i]->foreground == 0)
            {
                // if the job id is the current id we need
                if (jobs[i]->job_id == local_job_id)
                {
                    // print out the contents
                    printf("%d: ", jobs[i]->job_id);
                    int num_proc = 0;
                    for (p = jobs[i]->first_process; p; p = p->next)
                    {
                        if (num_proc == 0)
                        {
                            printf("%s ", p->name);
                            for (int i = 1; i < p->argc - 1; i++)
                            {
                                printf("%s ", p->argv[i]);
                            }
                            num_proc += 1;
                        }
                        else
                        {
                            printf("| ");
                            printf("%s ", p->name);
                            for (int i = 1; i < p->argc - 1; i++)
                            {
                                printf("%s ", p->argv[i]);
                            }
                        }
                    }
                    printf("& ");
                    printf("\n");
                    break;
                }
            }
        }
        // increment the job id we need and repeat search
        local_job_id += 1;
    }
}

/// @brief fg should move a process from the background to the foreground
/// @param argc the argument count
/// @param argv the argument vector
void wsh_fg(int argc, char *argv[])
{
    argc -= 1;
    // id was provided
    if (argc == 2)
    {
        // iterate over all jobs
        for (int i = 0; i < 256; i++)
        {
            // if the index of the jobs array is populated and the job is alive
            // if the job id is equal to the provided id
            if (jobs[i] != NULL && (jobs[i]->dead == 0) && jobs[i]->job_id == atoi(argv[1]))
            {
                /* Put the job into the foreground.  */
                tcsetpgrp(shell_terminal, jobs[i]->pgid);

                /* Wait for it to report.  */
                wait_for_job(jobs[i]);

                /* Put the shell back in the foreground.  */
                tcsetpgrp(shell_terminal, shell_pgid);

                /* Restore the shell’s terminal modes.  */
                tcgetattr(shell_terminal, &jobs[i]->tmodes);
                tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
                return;
            }
        }
    }
    // use most recent id
    else if (argc == 1)
    {
        // iterate over all jobs
        for (int i = 0; i < 256; i++)
        {
            // if the index of the jobs array is populated and the job is alive
            if (jobs[i] != NULL && (jobs[i]->dead == 0))
            {
                /* Put the job into the foreground.  */
                tcsetpgrp(shell_terminal, jobs[i]->pgid);

                /* Wait for it to report.  */
                wait_for_job(jobs[i]);

                /* Put the shell back in the foreground.  */
                tcsetpgrp(shell_terminal, shell_pgid);

                /* Restore the shell’s terminal modes.  */
                tcgetattr(shell_terminal, &jobs[i]->tmodes);
                tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
                return;
            }
        }
    }
    else
    {
        printf("USAGE: fg [job_id]\n");
    }
}

/// @brief bg should resume a process in the background - or run any suspended job in the background (!!!CURRENTLY BROKEN)
/// @param argc the argument count
/// @param argv the argument array
void wsh_bg(int argc, char *argv[])
{
    argc -= 1;
    // id was provided
    if (argc == 2)
    {
        for (int i = 0; i < 256; i++)
        {
            if (jobs[i] != NULL && (jobs[i]->dead == 0) && jobs[i]->job_id == atoi(argv[1]))
            {
                put_job_in_background(jobs[i], 1);
                return;
            }
        }
    }
    // use most recent id
    else if (argc == 1)
    {
        for (int i = 0; i < 256; i++)
        {
            if (jobs[i] != NULL && (jobs[i]->dead == 0))
            {
                put_job_in_background(jobs[i], 1);
                return;
            }
        }
    }
    else
    {
        printf("USAGE: fg [job_id]\n");
        // wsh_exit();
    }
}

/*
 * JOB CONTROL FUNCTIONS
 */

/// @brief launch a process in the background and set up its process group
/// @param p a pointer to a process struct
/// @param pgid a process group id of the parent job
/// @param infile the input stream of the process
/// @param outfile the output stream of the process
/// @param errfile the error stream of the process
/// @param foreground process is foreground indicator
void launch_process(process *p, pid_t pgid,
                    int infile, int outfile, int errfile,
                    int foreground)
{
    // set the process to a process group
    pid_t pid = getpid();
    if (pgid == 0)
        pgid = pid;
    if (getpid() != getsid(0))
    {
        setpgid(pid, pgid);
    }
    // give the process group control of the foreground
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

/// @brief The heart of the shell. Launch a job
/// @param j pointer to a job structure
/// @param foreground job is foreground indicator
void run_job(job *j, int foreground)
{
    process *p;
    pid_t pid;
    int mypipe[2], infile, outfile;

    infile = j->stdin;
    // iterate over all linked processes of the job
    for (p = j->first_process; p; p = p->next)
    {
        /* Set up pipes, if necessary.  */
        if (p->next)
        {
            if (pipe(mypipe) < 0)
            {
                perror("pipe");
                exit(1);
            }
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
            {
                j->pgid = pid;
            }
            if (getpid() != getsid(0))
            {
                setpgid(pid, j->pgid);
            }
        }

        /* Clean up after pipes.  */
        if (infile != j->stdin)
            close(infile);
        if (outfile != j->stdout)
            close(outfile);
        infile = mypipe[0];
    }

    // administer the job to the foreground or keep in background
    if (foreground)
        put_job_in_foreground(j, 0);
    else
        put_job_in_background(j, 0);
}

/*
 * INIT PROC/JOB FUNCTIONS
 */

/// @brief create a populated process struct
/// @param p the pointer to the process
/// @param name the name of the process
/// @param next the next process in the job (if any)
/// @param argc the argument count
/// @param argv the argument vector
/// @param pipe process is piped indicator
void populate_process_struct(process *p, char *name, process *next, int argc, char *argv[], int pipe)
{
    // allocate and set name
    p->name = malloc(256);
    strcpy(p->name, name);

    // allocate and set next pointer (for piping)
    p->next = (struct process *)malloc(sizeof(struct process));
    p->next = next;

    // only do this on pipes
    if (pipe)
    {
        argc += 1;
    }

    // allocate and set cmd args (for exec)
    p->argv = malloc(sizeof(*argv) * argc);
    for (int i = 0; i < argc - 1; i++)
    {
        p->argv[i] = strdup(argv[i]);
    }
    p->argv[argc] = NULL;

    p->argc = argc;

    p->dead = 0;
}

/// @brief create a populated job struct
/// @param j the pointer to the job
/// @param fp the first process in the job
/// @param foreground job is background indicator
/// @param piped job is piped indicator
void populate_job_struct(job *j, process *fp, int foreground, int piped)
{
    // set job id
    j->next = NULL;

    // allocate and set first process in job
    j->first_process = (struct process *)malloc(sizeof(struct process));
    j->first_process = fp;

    j->foreground = foreground;

    j->job_id = smallest_available_id();

    j->dead = 0;

    j->piped = piped;

    // set fds
    j->stdin = 0;
    j->stdout = 1;
    j->stderr = 2;
}

/*
 * RUNNER FUNCTIONS
 */

/// @brief run function for interactive mode
/// @return exit code
int runi()
{
    shell_terminal = STDIN_FILENO;

    /* Loop until we are in the foreground.  */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
        kill(-shell_pgid, SIGTTIN);

    // ignore job control signals
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);
    // signal(SIGCHLD, SIG_IGN);

    // Put ourselves in our own process group
    shell_pgid = getpid();
    if (getpid() != getsid(0))
    {
        if (setpgid(shell_pgid, shell_pgid) < 0)
        {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }
    }

    // Grab control of the terminal
    tcsetpgrp(shell_terminal, shell_pgid);
    // Save default terminal attributes for shell.
    tcgetattr(shell_terminal, &shell_tmodes);

    // iterate until an exit call is processed
    while (true)
    {
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
            if (num_pipes > 0)
            {
                // handle a background process
                if (bg)
                {
                    cmd_argv[cmd_argc - 2] = NULL;
                    cmd_argc -= 1;

                    int tmp_argc = cmd_argc - 2;
                    int num_itr = 0;

                    process *prev_p = (struct process *)malloc(sizeof(struct process));
                    process *first_p = (struct process *)malloc(sizeof(struct process));

                    // create a process for each pipe
                    while (num_itr != num_pipes + 1)
                    {
                        // we have to go backwards to link the processes together upon creation
                        char *tmp_argv[256];
                        int idx = 0;
                        for (int i = tmp_argc; i >= 0; i--)
                        {
                            tmp_argc -= 1;
                            if (strcmp(cmd_argv[i], "|") == 0)
                            {
                                break;
                            }

                            tmp_argv[idx] = cmd_argv[i];
                            idx += 1;
                        }

                        int tmp_argc_2 = reverse_array(tmp_argv, idx);

                        // first iteration, link process to nothing (is last proc of pipe)
                        if (num_itr == 0)
                        {
                            populate_process_struct(prev_p, tmp_argv[0], NULL, tmp_argc_2, tmp_argv, 1);
                        }
                        else
                        {
                            process *p = (struct process *)malloc(sizeof(struct process));
                            // last iteration, populate the first process of the pipe
                            if (num_itr == num_pipes)
                            {
                                populate_process_struct(first_p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                            }
                            // create a middle process and link it to the previous process
                            else
                            {
                                populate_process_struct(p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                                prev_p = p;
                            }
                        }

                        num_itr += 1;
                    }
                    // create the job and link it to the first process
                    job *j = (struct job *)malloc(sizeof(struct job));
                    populate_job_struct(j, first_p, 0, 1);

                    // add job to jobs array
                    jobs[curr_id] = j;
                    curr_id = curr_id + 1;

                    // run the job in the background
                    run_job(j, 0);
                }
                // handle a foreground process -- see above comments
                else
                {
                    int tmp_argc = cmd_argc - 2;
                    int num_itr = 0;

                    process *prev_p = (struct process *)malloc(sizeof(struct process));
                    process *first_p = (struct process *)malloc(sizeof(struct process));

                    while (num_itr != num_pipes + 1)
                    {
                        char *tmp_argv[256];
                        int idx = 0;
                        for (int i = tmp_argc; i >= 0; i--)
                        {
                            tmp_argc -= 1;
                            if (strcmp(cmd_argv[i], "|") == 0)
                            {
                                break;
                            }

                            tmp_argv[idx] = cmd_argv[i];
                            idx += 1;
                        }

                        int tmp_argc_2 = reverse_array(tmp_argv, idx);

                        if (num_itr == 0)
                        {
                            populate_process_struct(prev_p, tmp_argv[0], NULL, tmp_argc_2, tmp_argv, 1);
                        }
                        else
                        {
                            process *p = (struct process *)malloc(sizeof(struct process));
                            if (num_itr == num_pipes)
                            {
                                populate_process_struct(first_p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                            }
                            else
                            {
                                populate_process_struct(p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                                prev_p = p;
                            }
                        }

                        num_itr += 1;
                    }
                    job *j = (struct job *)malloc(sizeof(struct job));

                    populate_job_struct(j, first_p, 1, 1);

                    jobs[curr_id] = j;
                    curr_id = curr_id + 1;

                    run_job(j, 1);
                }
            }
            // non piped job
            else
            {
                // handle a background job
                if (bg)
                {
                    cmd_argv[cmd_argc - 2] = NULL;
                    cmd_argc -= 1;

                    process *p = (struct process *)malloc(sizeof(struct process));
                    job *j = (struct job *)malloc(sizeof(struct job));

                    populate_process_struct(p, cmd_argv[0], NULL, cmd_argc, cmd_argv, 0);
                    populate_job_struct(j, p, 0, 0);

                    jobs[curr_id] = j;
                    curr_id += 1;

                    run_job(j, 0);
                }
                // it is a foreground job or a built-in call
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
                    // run foreground job
                    else
                    {
                        process *p = (struct process *)malloc(sizeof(struct process));
                        job *j = (struct job *)malloc(sizeof(struct job));

                        populate_process_struct(p, cmd_argv[0], NULL, cmd_argc, cmd_argv, 0);
                        populate_job_struct(j, p, 1, 0);

                        jobs[curr_id] = j;
                        curr_id = curr_id + 1;

                        run_job(j, 1);
                    }
                }
            }
        }
    }
    return 0;
}

/// @brief batch mode runner function
/// @param batch_file the file of batch commands
/// @return exit code
int runb(char *batch_file)
{
    shell_terminal = STDIN_FILENO;

    /* Loop until we are in the foreground.  */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
        kill(-shell_pgid, SIGTTIN);

    // ignore job control signals
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);
    // signal(SIGCHLD, SIG_IGN);

    // Put ourselves in our own process group
    shell_pgid = getpid();
    if (getpid() != getsid(0))
    {
        if (setpgid(shell_pgid, shell_pgid) < 0)
        {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }
    }

    // Grab control of the terminal
    tcsetpgrp(shell_terminal, shell_pgid);
    // Save default terminal attributes for shell.
    tcgetattr(shell_terminal, &shell_tmodes);

    printf("%s\n", batch_file);

    // open file and iterate line by line
    FILE *file = fopen(batch_file, "r");

    char inner_line[256];
    char inner_cmd[256];
    while (fgets(inner_line, sizeof(inner_line), file))
    {
        strcpy(inner_cmd, inner_line);
        // remove newline
        if (inner_cmd[strlen(inner_cmd) - 1] == '\n')
        {
            inner_cmd[strlen(inner_cmd) - 1] = '\0';
        }

        int num_pipes = 0;
        int bg = 0;

        // parse command
        char tmp_cmd[256];
        strcpy(tmp_cmd, inner_cmd);
        int cmd_argc = 0;

        char *cmd_seg = strtok(tmp_cmd, " ");
        while (cmd_seg != NULL)
        {
            cmd_argc += 1;
            cmd_seg = strtok(NULL, " ");
        }
        // add room for NULL termination
        cmd_argc += 1;

        strcpy(tmp_cmd, inner_cmd);
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

        // handle each command whatever it is -- see runi comments
        if (cmd_argc - 1 > 0)
        {
            if (num_pipes > 0)
            {
                if (bg)
                {
                    cmd_argv[cmd_argc - 2] = NULL;
                    cmd_argc -= 1;

                    int tmp_argc = cmd_argc - 2;
                    int num_itr = 0;

                    process *prev_p = (struct process *)malloc(sizeof(struct process));
                    process *first_p = (struct process *)malloc(sizeof(struct process));

                    while (num_itr != num_pipes + 1)
                    {
                        char *tmp_argv[256];
                        int idx = 0;
                        for (int i = tmp_argc; i >= 0; i--)
                        {
                            tmp_argc -= 1;
                            if (strcmp(cmd_argv[i], "|") == 0)
                            {
                                break;
                            }

                            tmp_argv[idx] = cmd_argv[i];
                            idx += 1;
                        }

                        int tmp_argc_2 = reverse_array(tmp_argv, idx);

                        if (num_itr == 0)
                        {
                            populate_process_struct(prev_p, tmp_argv[0], NULL, tmp_argc_2, tmp_argv, 1);
                        }
                        else
                        {
                            process *p = (struct process *)malloc(sizeof(struct process));
                            if (num_itr == num_pipes)
                            {
                                populate_process_struct(first_p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                            }
                            else
                            {
                                populate_process_struct(p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                                prev_p = p;
                            }
                        }

                        num_itr += 1;
                    }
                    job *j = (struct job *)malloc(sizeof(struct job));

                    populate_job_struct(j, first_p, 0, 1);

                    jobs[curr_id] = j;
                    curr_id = curr_id + 1;

                    run_job(j, 0);
                }
                else
                {
                    int tmp_argc = cmd_argc - 2;
                    int num_itr = 0;

                    process *prev_p = (struct process *)malloc(sizeof(struct process));
                    process *first_p = (struct process *)malloc(sizeof(struct process));

                    while (num_itr != num_pipes + 1)
                    {
                        char *tmp_argv[256];
                        int idx = 0;
                        for (int i = tmp_argc; i >= 0; i--)
                        {
                            tmp_argc -= 1;
                            if (strcmp(cmd_argv[i], "|") == 0)
                            {
                                break;
                            }

                            tmp_argv[idx] = cmd_argv[i];
                            idx += 1;
                        }

                        int tmp_argc_2 = reverse_array(tmp_argv, idx);

                        if (num_itr == 0)
                        {
                            populate_process_struct(prev_p, tmp_argv[0], NULL, tmp_argc_2, tmp_argv, 1);
                        }
                        else
                        {
                            process *p = (struct process *)malloc(sizeof(struct process));
                            if (num_itr == num_pipes)
                            {
                                populate_process_struct(first_p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                            }
                            else
                            {
                                populate_process_struct(p, tmp_argv[0], prev_p, tmp_argc_2, tmp_argv, 1);
                                prev_p = p;
                            }
                        }

                        num_itr += 1;
                    }
                    job *j = (struct job *)malloc(sizeof(struct job));

                    populate_job_struct(j, first_p, 1, 1);

                    jobs[curr_id] = j;
                    curr_id = curr_id + 1;

                    run_job(j, 1);
                }
            }
            else
            {
                if (bg)
                {
                    cmd_argv[cmd_argc - 2] = NULL;
                    cmd_argc -= 1;

                    process *p = (struct process *)malloc(sizeof(struct process));
                    job *j = (struct job *)malloc(sizeof(struct job));

                    populate_process_struct(p, cmd_argv[0], NULL, cmd_argc, cmd_argv, 0);
                    populate_job_struct(j, p, 0, 0);
                    // populate_job_struct(j, p, 0, getpgid(getpid()));

                    jobs[curr_id] = j;
                    curr_id += 1;

                    run_job(j, 0);
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
                    // run fg child process/job
                    else
                    {
                        // run process in a job in the foreground
                        process *p = (struct process *)malloc(sizeof(struct process));
                        job *j = (struct job *)malloc(sizeof(struct job));

                        populate_process_struct(p, cmd_argv[0], NULL, cmd_argc, cmd_argv, 0);
                        populate_job_struct(j, p, 1, 0);

                        jobs[curr_id] = j;
                        curr_id = curr_id + 1;

                        run_job(j, 1);
                    }
                }
            }
        }
    }
    fclose(file);
    return 0;
}

/////

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
