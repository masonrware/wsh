# P3: Wisconsin Shell

Author: Mason R. Ware (mrware@wisc.edu, cs login: mware)

***

# Table of Contents
- [P3: Wisconsin Shell](#p3-wisconsin-shell)
- [Table of Contents](#table-of-contents)
- [What Works and What Doesn't](#what-works-and-what-doesnt)
  - [What Works](#what-works)
  - [What Doesn't](#what-doesnt)
- [High-Level Architecture](#high-level-architecture)
  - [Command Parsing](#command-parsing)
  - [Executional Decision Making](#executional-decision-making)
  - [Running a job and process](#running-a-job-and-process)
  - [Moving a Process to the Foreground](#moving-a-process-to-the-foreground)

***

# What Works and What Doesn't

## What Works
In my current implementation, to the best of my knowledge, just about everything works. This includes executing foreground and background processing, administering process groups, keeping track of running jobs, handling piped jobs, handling batch runs, and handling built in commands. 

## What Doesn't
The main thing that does not function in my final implementation is the `bg` built in command and the signal handling for `SIGTSTP` associated with crtl-z. This causes my implementation to partly fail the job control job.

***

# High-Level Architecture

My implementation has two runner functions for each mode the terminal can run in: one for batch and one for interactive. There are two key differences between these two runner functions:
1. Batch parses a file for every command where interactive uses the stdin for every command
2. Interactive runs in an indefinite while loop, while batch runs in a while loop that iterates over every line of the file

## Command Parsing

Both functions parse a command and look for certain characters:
- any amount of `|`s describe a piped command (multiple processes need to be run)
- an `&` at the end of the command means it is to be run in the background
- any of the key words that correspond to a built-in command so that no process is run and the built-in function is just invoked

## Executional Decision Making
Once the command is parsed, this information is stored in respective pieces of data. Then, there is a large if-then-else conditional that follows the logic:
if piped:
    if background:
        ...
        run
    else (foreground):
        ...
        run
else:
    if background:
        ...
        run
    else:
        if builtin1:
            builtin1()
        else if builtin2:
            builtin2()
        ...
        else (foreground):
            ...
            run

Within each ellipses, there is process creation and job creation. This entails the population of process structure(s) and a job structure to execute the task. This process differs slightly depending on which condition evaluates to true: if we are in the pipe conditional, then we need to iterate backwards over the commands in argv. This is because I create a process using the `populate_process_struct` function that takes a process to point to as a parameter (this is how I implement piping, a job->first_process->next_process->next_process.... with overwritten file descriptors for in between processes). Thus, I need to create the last process first, then link the second to last process to that process, then the third to last to the second to last, all the way on to the first process. Then, create a job and point it at the head of that chain.

If I am in the else (not piped) conditional, then the process is a bit simpler as we only need to create a job with one process. So, the only difference becomes if it is background or foreground and that manifests itself in the booleans (0 or 1) passed to the multiple functions used to create a process, create a job, and run a job. Each of these takes an int to represent whether or not we are running the process in the foreground or background, something I use later on to determine many things.

## Running a job and process

Once a job has been created, it is added to a jobs array and then run with the associated boolean. The function that does this is called `run_job()`. Run job works to first iterate over all processes the job points to by traversing the chain of processes linked to the first_process member of the job struct. For each, it configures the outfile and infile to configure piping (if needed). Then, it forks the child process, calls a function `launch_process` with the process information for each process in the chain, and handles the post-mortem. The post-mortem is the stuff needed to do after a child process is launched, such as reset the process group ids, clean up the piping, and most importantly, directing the launched process (launched in background == not waited for by default) into the foreground or background.

Stepping back a bit, `launch_process` works to create a process and delegate it to a process group. First, it sets the process to a process group id and then it calls dup2 to actually configure the file descriptors. Finally, it calls execvp and exits.

Back to `run_job`, once a process is launched, the foreground boolean mentioned earlier is directed to the foreground or background. 

## Moving a Process to the Foreground

Moving a process to the background doesn't mean anything as `launch_process` does not wait for the process by default, it therefore launches a process in the background by default. Thus, I will cover moving a process to the foreground.

Essentially, the only work that needs to be done here is putting the job in the foreground process group by id, waiting for the job using a function called `wait_for_job()` and then returning the shell to the foreground process group once everything is done.

`wait_for_job()` operates a do-while loop that either waits for any process to finish or for processes associated to a given process group (piping differentiates these two). Each iteration, functions to manage the job status are called and return a boolean. When they all evaluate to true, the do-while ends and the fg job can be marked dead.


This concludes the high-level overview of the shell, everything else would be describing implementation details and I will leave that for the code and its comments.

Thank you :)

***