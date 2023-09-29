#include "wsh.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define PATH "/bin"

// built-in commands
void exit() {
  exit(0);
}

void cd() {
  // test comment for git
}

void jobs() {

}

void fg() {

}

void bg() {

}


int runi() {
  while(true) {
    printf("wsh> ");

    // collect user cmd
    char cmd[256];
    char *line = fgets(cmd, sizeof(cmd), stdin);

    // check if EOF is reached/input
    if(line == NULL){
	  exit();
    }

    // remove newline
    if (cmd[strlen(cmd)-1] == '\n')
      cmd[strlen(cmd)-1] = '\0';
    
    // TODO:  potentially seperate out arguments provided (ls -la file)

    printf("you typed: %s\n", cmd);
    if(strcmp(cmd, "exit")==0) {
      exit();
    }
  }
  return 0;
}

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

