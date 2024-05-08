/* Author: Ishwari Joshi
   This program implements a Unix command line interpreter (CLI) or, as it is more commonly known, a Unix shell. 
   The shell is called dash (short for DAllas SHell).
   It supports interactive command input at dash> prompt while running and script file input as a command line argument.
   Compile using "gcc dash.c –o dash -Wall -Werror -O" on a Unix system.
*/

/* include header files (examples for library usage included) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     // for strtok() and strcmp()
#include <unistd.h>     // for fork(), execv(), and pid_t
#include <sys/types.h>  // for fork(), waitpid(), and pid_t
#include <sys/wait.h>   // for waitpid()
#include <fcntl.h>      // for open()
#include <ctype.h>      // for isstring()

// BUF_SIZE was changed to 32 and 256 to test testcase-2-longcmd.txt and the output was verified to be the same in all cases
#define BUF_SIZE 1024   // buffer size

/* function declarations */
char* read_input();
void process(char* input, char*** path);
int check_empty_input(char* input);
char** parse_input(char* input);
char** parse_cmds(char* input);
pid_t exec_command(char** arrTok, char** path, int redirection);
void wait_for_cmds(pid_t pid[], int parallel_cmd);
void write_error();
int check_command(char** arrTok);
int check_parallel(char* input);
int check_redirect(char* input);
int check_path(char** arrTok);
void dash_exit(char** arrTok);
void dash_exit2(char** arrTok, int parallel_cmd, pid_t pid[]);
void dash_cd(char** arrTok);
char** dash_path(char** arrTok);
int count_tokens(char** arr);
void which_built_in(char** arrTok, int parallel_cmd, pid_t pid[]);

int exit_not_called = 1;        // initialize exit as not called
char* built_in_commands[] = {   // char pointer array listing built-in commands
    "exit",
    "cd",
    "path"
};

int main(int argc, char *argv[])
{
    char** path = malloc(sizeof(char*));    // allocate memory for path variable
    path[0] = "/bin";                       // initialize initial shell path directory
    
    /* argc is how many command line arguments are passed to run program
    ./dash --> argc = 1 (no argument)
    ./dash batch.txt --> argc = 2 (1 argument)
    anything else is an error */
    if (argc == 1) {

        /* Interactive mode. repeatedly prints a prompt dash> and processes
        the input (parses the input, executes the command specified on that 
        line of input, and waits for the command to finish.)
        This is repeated until the user types exit. */
        while(exit_not_called) {
            printf("dash> ");
            char* input = read_input();
            process(input, &path);
        }
    }
    else if (argc == 2) {

        /* Batch mode. reads input from a batch file and executes commands 
        from therein. */
        char* file_name = argv[1]; // argv is pointer array containing each argument passed to the program
        FILE* input_file = fopen(file_name, "r");
        if (!input_file) {
            write_error();
            exit(1);
        }

        char* input = NULL;
        size_t bufsize = 0; 
        // read input line by line from input file
        while (getline(&input, &bufsize, input_file) != -1) {
            process(input, &path);
            input = NULL;
        }

        // getline returns the value -1 if an error occurs or if end-of-file (eof) is reached
        // when eof is reached, exit the shell
        if (feof(input_file)) {
            fclose(input_file);
            exit(0);
        }
        // error occurred
        else {
            fclose(input_file);
            write_error();
        }
    }
    // argc > 2
    else {
        write_error();
        exit(1);
    }
    free(path);     // free malloced path once done using the path variable
    return EXIT_SUCCESS;
}

/*
 *  Function:  read_input
 *  --------------------
 *  reads standard input from the keyboard
 *
 *  returns: char pointer to the input line
 */
char* read_input() {
    char* input = NULL;
    size_t bufsize = 0;

    // getline allocates a new buffer if input points to a 
    // NULL pointer and expands the capacity of the buffer in bufsize,
    // updating both accordingly. No need to malloc

    // getline returns the value -1 if an error occurs or if end-of-file is reached

    if (getline(&input,&bufsize,stdin) == -1) {
        // exit at eof
        if (feof(stdin)) {
            exit(0);
        }
        // error occurred
        else {
            write_error();
        }
    }
    return input;
}

/*
 *  Function:  process
 *  --------------------
 *  parses the input line and sends it to be executed
 *
 *  input: char pointer to the input line
 *  path: the current path specified (a pointer to the char** path variable used 
 *  elsewhere is passed here to write in this function to the value behind the pointer)
 */
void process(char* input, char*** path) {
    // checks for only white space on input line
    // if that is the case, another dash> prompt is printed
    if (check_empty_input(input) == 1) {
        return;
    }
    
    // there is input on the line
    else {
        char** arrTok = NULL;   /* arrTok holds the tokens if no parallel commands
                                or each command if there are parallel commands.
                                arrTok[index] is char* type. */

        int parallel_cmd = check_parallel(input);   // check for &
        if (parallel_cmd == -1) {
            write_error();
            return;
        }
        int redirection = -1;   // initialize redirection to a value that is not possible
        /* parse commands by &.
        ex. cmd1 & cmd2 args1 args2.
        arrTok[0] = cmd1, arrTok[1] = cmd2 args1 args2 */
        if (parallel_cmd > 0) {
            arrTok = parse_cmds(input);
        }
        // no parallel commands
        else {
            redirection = check_redirect(input);    // check for >
            // multiple redirection operators or cases such as the following
            // cmd > , > file , cmd > file1 file2 not allowed
            if (redirection > 1 || redirection < 0) {
                write_error();
                return;
            }
            arrTok = parse_input(input);
        }

        pid_t pid[parallel_cmd + 1];    // initialize array to hold pids from each fork

        // no parallel commands, so run like a normal input line
        if (parallel_cmd == 0) {
            int i = 0;
            // check if command is built-in
            // if it is, run the implementation of the command,
            // and do send it to execute
            int is_built_in = check_command(arrTok);
            if (is_built_in == 1) {
                if (check_path(arrTok) == 1) {
                    *path = dash_path(arrTok);  // change path
                }
                else {
                    which_built_in(arrTok, parallel_cmd, pid);  // cd or exit
                }
            }
            // not built-in command so go to execute and wait for the command to finish
            else {
                pid[i] = exec_command(arrTok, *path, redirection);
                wait_for_cmds(pid, parallel_cmd);
            }
        }
        // same logic as no parallel commands but first parse the commands into individual tokens
        else {
            char** arr = NULL;
            int i;
            /* for loop to execute each command in parallel before waiting for any of them to finish.
            end condition (parallel_cmd + 1) is the number of commands = number of &'s + 1 */
            for (i = 0; i < parallel_cmd + 1; i++) {
                redirection = check_redirect(arrTok[i]);
                // if redirection error, move onto the next command
                if (redirection > 1 || redirection < 0) {
                    write_error();
                    continue;
                }
                arr = parse_input(arrTok[i]);
                // if no command, move onto the next command (ex. cmd & cmd arg1 &)
                if (arr[0] == NULL) {
                    continue;
                }
                int is_built_in = check_command(arr);
                if (is_built_in == 1) {
                    if (check_path(arr) == 1) {
                        *path = dash_path(arr);  // change path
                    }
                    else {
                        which_built_in(arr, parallel_cmd, pid);     // cd or exit
                    }
                }
                else {
                    // store pid at index i (1 pid per command)
                    pid[i] = exec_command(arr, *path, redirection);
                }
                free(arr);  // free malloced arr for every iteration of loop
            }
            // after starting all processes, wait for them to complete
            wait_for_cmds(pid, parallel_cmd);
        }
    }
}

/*
 *  Function:  check_empty_input
 *  --------------------
 *  checks if the input line is only whitespace (ex. user pressed enter before entering command)
 *
 *  input: char pointer to the input line
 *  
 *  returns: 0 if there is input to be processed
 *           1 if there is only whitespace 
 */
int check_empty_input(char* input) {
    // dereference pointer to access every character in input string
    while (*input != '\0') {
        // not whitespace
        if (isspace(*input) == 0) { 
            return 0;
        }
        input++;
    }
    return 1;
}

/*
 *  Function:  parse_input
 *  --------------------
 *  parses input by delimiters including space, tab, newline, and redirection operator
 * 
 *  input: char pointer to the input line
 *  
 *  returns: char** to the tokenized array
 */
char** parse_input(char* input) {
    char* token;
    char** arrTok;
    int buf_size = BUF_SIZE;
    arrTok = malloc(BUF_SIZE * sizeof(char*));  // allocate memory
    if(!arrTok)
    {
        write_error();
        exit(1);
    }
    
    int numTokens = 0;
    token = strtok(input, " \t\n>");    // get first token
    while (token != NULL) {
        arrTok[numTokens] = token;      // store token in arrTok
        numTokens++;

        // if there are more tokens in the input than allocated for,
        // increase the buffer size and reallocate memory
        if (numTokens >= buf_size) {
            buf_size += BUF_SIZE;
            arrTok = realloc(arrTok, buf_size * sizeof(char*));
            if (!arrTok) {
                write_error();
                exit(1);
            }
        }

        token = strtok(NULL, " \t\n>"); // get the next token.
                                        // NULL holds the current searching position in the input
    }
    arrTok[numTokens] = NULL;   // set the last index to NULL.
                                // execv requires the array of pointers to be 
                                // terminated by a NULL pointer.
    return arrTok;
}

/*
 *  Function:  parse_cmds
 *  --------------------
 *  parses input by ampersand (&) delimiter
 * 
 *  input: char pointer to the input line
 *  
 *  returns: char** to the tokenized array
 */
char** parse_cmds(char* input) {
    char* cmd;
    char** arrCmd;
    int buf_size = BUF_SIZE;
    arrCmd = malloc(buf_size * sizeof(char*));  // allocate memory
    if(!arrCmd)
    {
        write_error();
        //exit(1);
    }

    int numCmds = 0;
    cmd = strtok(input, "&");       // get first token
    while (cmd != NULL) {
        arrCmd[numCmds] = cmd;      // store token in arrTok
        numCmds++;

        // if there are more tokens in the input than allocated for,
        // increase the buffer size and reallocate memory
        if (numCmds >= buf_size) {
            buf_size += BUF_SIZE;
            arrCmd = realloc(arrCmd, buf_size * sizeof(char*));
            if (!arrCmd) {
                write_error();
                exit(1);
            }
        }

        cmd = strtok(NULL, "&");    // get the next token.
                                    // NULL holds the current searching position in the input
    }
    arrCmd[numCmds] = NULL;         // set the last index to NULL
    return arrCmd;
}

/*
 *  Function:  exec_command
 *  --------------------
 *  creates a process ID (pid) for executing every command using fork(), checks each path
 *  for access to the command, redirects standard output and standard error output of the
 *  command to a file if redirection is present, and executes the command using execv
 * 
 *  arrTok: char** (pointer to pointer) to the tokenized command
 *  path: the current path(s) specified to search through 
 *  redirection: 1 is redirection is present without errors
 *  
 *  returns: pid of command that is executed 
 */
pid_t exec_command(char** arrTok, char** path, int redirection) {
    pid_t pid = fork();     // returns a pid

    // could not create a child process
    /* NOTE: if ulim -u 90 is added to the .bashrc file in your home directory,
    pid will be less than 0 if more than 90 processes are being executed */
    if (pid < 0) {
        write_error();
    }
    
    // child process successfully created
    else if (pid == 0) {
        int buf_size = BUF_SIZE;
        char* path_access = malloc(buf_size * sizeof(char));    // allocate memory
        char* slash = malloc(5 * sizeof(char));
        int path_len = count_tokens(path);
        // empty path
        if (path_len == 0) {
            write_error();
            exit(1);
        }
        int j;
        // search through every path specified
        for (j = 0; j < path_len; j++) {
            strcpy(path_access,path[j]);    // add path to path_access
            strcpy(slash,"/");
            /* concatenate path_access with a slash and command executable (first token).
            ex. path_access/executable */
            strcat(path_access,strcat(slash,arrTok[0]));
            // access checks if a particular file exists in a directory and is executable
            // if it is, don't need to look through any more paths 
            if (access(path_access,X_OK) == 0) {
                break;
            }
            // if all paths have been searched and access still fails, it is an error
            else if ((j == path_len - 1) && access(path_access,X_OK) == -1) {
                if (redirection != 1) {
                    write_error();
                    exit(1);
                }
            }
        }
        // write standard output/error to file
        if (redirection == 1) {
            int file_index = count_tokens(arrTok) - 1;  // file name index
            close(1);   // close standard output on screen
            close(2);   // close standard error on screen
            // open file descriptor for writing, create file if it does not exist, and truncate/overwrite if it exists
            int fd = open(arrTok[file_index], O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
            // invalid file descriptor
            if(fd == -1) {
                write_error();
                exit(1);
            }
        
            // set file name index to NULL so it is not included as part of the executable command
            arrTok[file_index] = NULL;  

            // execv should not return. if it does, an error occurred 
            if (execv(path_access,arrTok) == -1) {
                // write the error to the file
                char error_message[30] = "An error has occurred\n";
                write(fd, error_message, strlen(error_message));
                close(fd);
                redirection = 0;
                exit(1);
            }
            close(fd);
            redirection = 0;
        }
        // no redirection
        else {
            // check if execv failed
            if (execv(path_access,arrTok) == -1) {
                write_error();
                exit(1);
            }
        }
        // free all variables used in function
        free(path_access);
        free(slash);
        free(arrTok);
    }
    return pid;
}

/*
 *  Function:  wait_for_cmds
 *  --------------------
 *  waits for process(es) to complete
 * 
 *  pid[]: array of pids for every process
 *  parallel_cmd: the number of parallel commands
 */
void wait_for_cmds(pid_t pid[], int parallel_cmd) {                
    int k;
    // wait for each command to finish
    for (k = 0; k < parallel_cmd + 1; k++) {
        int status;
        do {
            waitpid(pid[k], &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
}

/*
 *  Function:  write_error
 *  --------------------
 *  writes an error when an error of any type is encountered 
 */
void write_error() {
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}

/*
 *  Function:  check_redirect
 *  --------------------
 *  counts the number of redirection operators in 1 command
 * 
 *  input: char pointer to command
 * 
 *  returns: number of redirection operators in 1 command
 */
int check_redirect(char* input) {
    int redirect_count = 0;
    char* input_cpy = malloc(strlen(input) + 1);    // allocate memory
    strcpy(input_cpy, input);   // make a copy of the input so the original does not get destoyed
    
    // strchr searches for the first occurrence of '>' in input
    // ex. if input is ls > out.txt, occurence will be > out.txt
    char* occurence = strchr(input, '>');
    while (occurence != NULL) {
        redirect_count++;
        // search for another '>' starting at the next character
        occurence = strchr(occurence + 1, '>');
    }

    // look for errors with 1 redirection operator
    if (redirect_count == 1) {
        // get the input before the '>'
        char* before = strtok(input_cpy, ">");
        // get the rest of the input
        char* after = strtok(NULL, "\n");
        // if either is NULL, there is nothing before or nothing after the '>'
        // return -1 to indicate error
        if (before == NULL || after == NULL) {
            return -1;
        }
        // if there are any spaces after '>', get the input after the spaces
        char* files = strtok(after, " ");

        while (files != NULL) {
            // search for another file 
            files = strtok(NULL, " \t\n");
            // more than one file is an error
            if (files != NULL) {
                redirect_count++;   // incrementing the count above 1 indicates an error
            }
        }
    }
    free(input_cpy);    // free malloced variable
    return redirect_count;
}

/*
 *  Function:  check_parallel
 *  --------------------
 *  counts the number of ampersands
 * 
 *  input: char pointer to parallel command
 * 
 *  returns: number of & in parallel command
 */
int check_parallel(char* input) {
    int parallel_count = 0;
    char* input_cpy = malloc(strlen(input) + 1);    // allocate memory
    strcpy(input_cpy, input);   // make a copy of the input so the original does not get destoyed

    // search for the first occurrence of '&' in input
    char* occurence = strchr(input, '&');
    while (occurence != NULL) {
        parallel_count++;
        // search for another '&' starting at the next character
        occurence = strchr(occurence + 1, '&');
    }
    
    // look for errors with 1 &
    if (parallel_count == 1) {
        // get the input before the '&'
        char* before = strtok(input_cpy, "&");
        // get the input from before the & to the newline
        // before should contain something (ex. before will be ls for ls& )
        before = strtok(before, "\n");
        // if before is NULL, there is an error
        if (before == NULL){
            return -1;      // return -1 to indicate error
        }
    }
    free(input_cpy);    // free malloced variable
    return parallel_count;
}

/*
 *  Function:  check_command
 *  --------------------
 *  checks if command is built-in
 * 
 *  arrTok: char** with first index containing command to check
 * 
 *  returns: 1 if command is built-in or 0 otherwise
 */
int check_command(char** arrTok) {
    // get length of built_in_commands array
    int how_many_built_in = sizeof(built_in_commands) / sizeof(char*);
    int i;
    for (i = 0; i < how_many_built_in; i++) {
        // compare first token with built_in_commands array
        if (strcmp(arrTok[0], built_in_commands[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 *  Function:  check_path
 *  --------------------
 *  checks if path is called
 * 
 *  arrTok: char** with first index containing command to check
 * 
 *  returns: 1 if command is path or 0 otherwise
 */
int check_path(char** arrTok) {
    // built_in_commands[2] contains path
    if (strcmp(arrTok[0], built_in_commands[2]) == 0) {
            return 1;
        }
    return 0;
}

/*
 *  Function:  dash_exit
 *  --------------------
 *  built-in implementation of exit command
 * 
 *  arrTok: char** that has been tokenized
 */
void dash_exit(char** arrTok) {
    // count arguments in arrTok (excluding exit)
    int args = count_tokens(arrTok) - 1;
    // error to pass any arguments to exit
    if (args != 0) {
        write_error();
    }
    // call the exit system call with 0 as parameter
    else {
        exit_not_called = 0;
        exit(0);
    }
}

/*
 *  Function:  dash_exit2
 *  --------------------
 *  built-in implementation of exit command if there are parallel commands.
 *  if exit is called in a parallel command, the program must wait for all commands
 *  to finish before exiting
 * 
 *  arrTok: char** that has been tokenized
 *  parallel_cmd: the number of parallel commands
 *  pid[]: array of pids for each process
 */
void dash_exit2(char** arrTok, int parallel_cmd, pid_t pid[]) {
    // count arguments in arrTok (excluding exit)
    int args = count_tokens(arrTok) - 1;
    // error to pass any arguments to exit
    if (args != 0) {
        write_error();
    }
    else {
        exit_not_called = 0;
        // wait before exiting
        if (parallel_cmd > 0) {
            wait_for_cmds(pid, parallel_cmd);
        }
        exit(0);    // call the exit system call with 0 as parameter
    }
}

/*
 *  Function:  dash_cd
 *  --------------------
 *  built-in implementation of cd command
 * 
 *  arrTok: char** that has been tokenized
 */
void dash_cd(char** arrTok) {
    // count arguments in arrTok (excluding cd)
    int args = count_tokens(arrTok) - 1;
    // anything other than 1 argument is error
    if (args == 0 || args > 1) {
        write_error();
    }
    else {
        // change directory to argument 
        if (chdir(arrTok[1]) != 0) {
            write_error();
        }
    }
}

/*
 *  Function:  dash_path
 *  --------------------
 *  built-in implementation of path command
 * 
 *  arrTok: char** that has been tokenized
 * 
 *  returns: char** to path variable 
 */
char** dash_path(char** arrTok) {
    char** path_changed = malloc(BUF_SIZE * sizeof(char*));     // allocate memory
    // path arguments are given
    if (arrTok[1] != NULL) {
        int args = count_tokens(arrTok) - 1;
        int i;
        for (i = 0; i < args; i++) {
            // set path to the arrTok arguments supplied in the path command
            path_changed[i] = arrTok[i+1];
        }
        path_changed[args] = NULL;      // set last index to NULL
    }
    // path specified to be empty
    else {
        path_changed[0] = NULL;
    }
    return path_changed;
}

/*
 *  Function:  count_tokens
 *  --------------------
 *  counts the length of the array
 * 
 *  arr: char** 
 * 
 *  returns: length of filled indices in array
 */
int count_tokens(char** arr) {
    int len = -1;
    while (arr[++len] != NULL) {}
    return len;
}

/*
 *  Function:  which_built_in
 *  --------------------
 *  sends the command to the function that implements the built-in command
 * 
 *  arrTok: char** that has been tokenized
 *  parallel_cmd: the number of parallel commands
 *  pid[]: array of pids for each process
 */
void which_built_in(char** arrTok, int parallel_cmd, pid_t pid[]) {
    // check_path function sends command to path built-in function
    if (strcmp(arrTok[0], built_in_commands[0]) == 0) {
        // 2 exit implementations to choose from
        if (parallel_cmd > 0) {
            dash_exit2(arrTok, parallel_cmd, pid);
        }
        else {
            dash_exit(arrTok);
        }
    }
    else if (strcmp(arrTok[0], built_in_commands[1]) == 0) {
        dash_cd(arrTok);
    }
}