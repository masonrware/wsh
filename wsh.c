#include "wsh.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#define PATH "/bin:/usr/bin"

// built-in commands
void wsh_exit() {
  exit(0);
}

void cd(char *path) {
  if (chdir(path)!=0) {
    printf("Error: chdir to %s failed.\n", path);
    wsh_exit();
  }
}

void jobs() {

}

void fg() {

}

void bg() {

}

// run function for interactive mode
int runi() {
  while(true) {
    printf("wsh> ");

    // collect user cmd
    char cmd[256];
    char *line = fgets(cmd, sizeof(cmd), stdin);

    // check if EOF is reached/input
    if(line == NULL){
	  wsh_exit();
    }

    // remove newline
    if (cmd[strlen(cmd)-1] == '\n')
      cmd[strlen(cmd)-1] = '\0';

    // parse command     
    char tmp_cmd[256];
    strcpy(tmp_cmd, cmd);
    int cmd_argc = 0;

    char *cmd_seg = strtok(tmp_cmd, " ");
    while(cmd_seg !=NULL) {
      cmd_argc+=1;
      cmd_seg = strtok(NULL, " ");
    } 
    
    strcpy(tmp_cmd, cmd);
    char *cmd_argv[cmd_argc];    
    cmd_seg = strtok(tmp_cmd, " ");
    for(int i = 0; i<cmd_argc; i++) {
      cmd_argv[i] = cmd_seg;
      cmd_seg = strtok(NULL, " ");
    }
    
    // exit
    if(strcmp(cmd_argv[0], "exit") == 0){
      wsh_exit();
    } 
    // cd
    else if(strcmp(cmd_argv[0], "cd") == 0) {
      
      printf("Handle %s\n", cmd_argv[0]);
    }
    // jobs
    else if(strcmp(cmd_argv[0], "jobs") == 0) {
      printf("Handle %s\n", cmd_argv[0]);
    } 
    // fg
    else if(strcmp(cmd_argv[0], "fg") == 0) {
      printf("Handle %s\n", cmd_argv[0]);
    } 
    // bg
    else if(strcmp(cmd_argv[0], "bg") == 0) {
      printf("Handle %s\n", cmd_argv[0]);
    }
    // search path
    else {
      int found_exe = 0;

      char tmp_path[256];
      char tmp_subpath[256];      

      strcpy(tmp_path, PATH);

      char *subpath = strtok(tmp_path, ":");
      
      while (subpath != NULL) {
        strcpy(tmp_subpath, subpath);
        
        strcat(tmp_subpath, "/");
        strcat(tmp_subpath, cmd_argv[0]);
        
        if (access(tmp_subpath, X_OK) != -1) {
           found_exe = 1;
           // TODO: run executable
           printf("FOUND %s EXECUTABLE @ %s\n", cmd_argv[0], tmp_subpath); 
           break;
        }
        subpath = strtok(NULL, ":");
      }
      if (found_exe == 0) {
        // didn't find .exe for cmd
        printf("ERROR: could not find %s executable on provided path.\n", cmd_argv[0]);
        wsh_exit();
      }
    }
  }
  return 0;
}

// run function for batch mode
int runb(char *batch_file) {
  // open batch file and iterate over it
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 1 || argc > 2) {
    printf("Usage: ./wsh [batch_file]\n");
	exit(1);
  }

  // interactive mode  
  if (argc == 1) {
    runi();
  }
  // batch mode
  else if (argc == 2) {
    char *file_name = argv[1];
    runb(file_name);
  }

  return 0;
}

