#include "wsh.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#define PATH "/bin"

// built-in commands
void wsh_exit() {
  exit(0);
}

void cd() {

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
    
    // TODO:  potentially separate out arguments provided (ls -la file) 
    
    // exit
    if(strcmp(cmd, "exit") == 0){
      wsh_exit();
    } 
    // cd
    else if(strcmp(cmd, "cd") == 0) {
      printf("Handle %s\n", cmd);
    }
    // jobs
    else if(strcmp(cmd, "jobs") == 0) {
      printf("Handle %s\n", cmd);
    } 
    // fg
    else if(strcmp(cmd, "fg") == 0) {
      printf("Handle %s\n", cmd);
    } 
    // bg
    else if(strcmp(cmd, "bg") == 0) {
      printf("Handle %s\n", cmd);
    }
    // search path
    else {
      char *path = strtok(PATH, ":");
      printf("PATH: %s\n", path);
      // while (path != NULL) {
      //   strcat(path, "/");
      //   strcat(path, cmd);
      //   if (access(path, X_OK) != -1) {
      //     printf("FOUND %s EXECUTABLE @ %s\n", cmd, path); 
      //     break;
      //   }
      //   path = strtok(NULL, ":");
      // }
      // // didn't find .exe for cmd
      // printf("ERROR: could not find %s executable on provided path.\n", cmd);
      // wsh_exit();
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

