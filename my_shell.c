#include  <stdio.h>
#include  <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>
#include <assert.h>

#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 64
#define MAX_NUM_TOKENS 64
/*macro for mode of execution*/
#define SINGLE_FOREGROUND 323
#define SINGLE_BACKGROUND 324
#define SERIAL_FOREGROUND 325
#define PARALLEL_FOREGROUND 326
/******************BEGIN: Data Structures for Handling Process Management*********************/

typedef struct process{
	struct process * next;
	int pid;
	/*STATUS Field*/
	int foreground;
	int terminated;
	/*Temporaray place holder for argument
	should be freed after the process terminated
	and be reaped by the shell, the content char*
	should not be freed, because they're aliases with 
	the input token array, which will be freed at the end
	of the loop*/
	char **args;
} process;


typedef struct job_list{
	struct process * first;
	struct job_list * next;
} job_list;

/*utility function: add a process to a job
*
*/
void job_list_add_process(job_list *jobs, process *new_process){
	if (jobs -> first == NULL)
		jobs -> first = new_process;
	else{
		process *cur = jobs -> first;
		while (cur -> next != NULL) cur = cur -> next;
		cur -> next = new_process;
	}
}

/*add a new job to the existing job list in the front
*
*/
void add_next_job(job_list **dest_address, job_list *src){
	src -> next = *dest_address;

	*dest_address = src;
}

/*clean up the memory allocated to hold the process's argument
*
*/
void clean_up_process(process *process_to_free){
	while (process_to_free != NULL){
		for (int i = 0; process_to_free -> args[i] != NULL; ++i){
			//printf("I'm freeing string\n");
			free(process_to_free -> args[i]);
		}
		//printf("I'm freeing array\n");
		free(process_to_free -> args);
		process_to_free -> args = NULL;
		process_to_free = process_to_free -> next;
	}
}

/*clean up the memory to hold the process struct itself
*
*/
void free_process(process *process_to_free){
	if (process_to_free == NULL) return;
	free_process(process_to_free -> next);
	free(process_to_free);
}

/*clean up job list and all the process, this should only be done right before exit
*
*/
void clean_up_job_list(job_list *job_to_free){
	if (job_to_free == NULL) return;
	clean_up_job_list(job_to_free -> next);
	free_process(job_to_free -> first);
	//printf("I'm freeing \n");
	free(job_to_free);
}

/******************END: Data Structures for Handling Process Management*********************/

/* Splits the string by space and returns the array of tokens
*
*/
char **tokenize(char *line)
{
  char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
  char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
  unsigned long i;
  int tokenIndex = 0, tokenNo = 0;

  for(i =0; i < strlen(line); i++){

    char readChar = line[i];

    if (readChar == ' ' || readChar == '\n' || readChar == '\t'){
      token[tokenIndex] = '\0';
      if (tokenIndex != 0){
	tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
	strcpy(tokens[tokenNo++], token);
	tokenIndex = 0; 
      }
    } else {
      token[tokenIndex++] = readChar;
    }
  }
 
  free(token);
  tokens[tokenNo] = NULL ;
  return tokens;
}

/*
*parse the token array into complete command argument, seperated by "pattern"
*this helper function will be called repeatedly to get next argument
*return 0 if no more parse can be found
*/
int parse_arg(char **tokens, char **args){
	//track the position in the token
	static int index;
	//this round of parsing is over, re-initialize position tracer
	if (tokens[index] == NULL){
		index = 0;
		return 0;
	} 
	//start parsing
	//skip the pattern slot of next round
	if (strcmp(tokens[index], "&") == 0 || strcmp(tokens[index], "&&") == 0 || strcmp(tokens[index], "&&&") == 0){
		//parsing pattern is not allowed at the front of the argument
		if (index == 0){
			printf("Shell: Incorrect Command\n");
			return 0;
		}
		++index;
	} 
	int argNo = 0;
	for (; (tokens[index] != NULL && 
		!(strcmp(tokens[index], "&") == 0 || strcmp(tokens[index], "&&") == 0 || strcmp(tokens[index], "&&&") == 0)); ++index){
		args[argNo] = (char *)malloc(MAX_TOKEN_SIZE*sizeof(char));
		strcpy(args[argNo++], tokens[index]);
	}
	if (argNo == 0){
		index = 0;
	 	return 0;
	}
	//null-terminating the argument array
	args[argNo] = NULL;
	return 1;

}

/*Return a job data structure by parsing the token array, which 
* could be used to handle process fork and execution
*/
job_list *get_new_job(char **tokens){

	if (*tokens == NULL) return NULL;

	job_list *job = (job_list *)malloc(sizeof(job_list)); job -> first = NULL; job -> next = NULL;
	char **args = (char **)malloc(MAX_NUM_TOKENS*sizeof(char *));
	process *next_process;
	int count = 0;
	while (parse_arg(tokens, args)){
		++count;
		next_process = (process *)malloc(sizeof(process));
		next_process -> foreground = 1;
		next_process -> terminated = 0;
		next_process -> pid = 0;
		next_process -> next = NULL;
		next_process -> args = args;
		job_list_add_process(job, next_process);
		if ((args = (char **)malloc(MAX_NUM_TOKENS*sizeof(char *))) == NULL){
			printf("Error\n");
		}
	}

	return job;

}

/*Scan the token array to get the mode of execution
* '&' -> single_background
* '&&' -> serial_foreground
* '&&&' -> 
*/
int get_mode_of_execution(char **tokens){
	for (int i = 0; tokens[i] != NULL; ++i){
		//serial multiple foreground
		if (strcmp(tokens[i], "&&") == 0){
			return SERIAL_FOREGROUND;
		} else if (strcmp(tokens[i], "&&&") == 0){ //parallel multiple foreground
			return PARALLEL_FOREGROUND;
		} else if (strcmp(tokens[i], "&") == 0){ //single background
			return SINGLE_BACKGROUND;
		}

	}
	//if none of the above token is found, execute single forground
	return SINGLE_FOREGROUND;
}

/*execute internal command, return 1 if this is an internal command, so there's 
* no need to go for system command, 0 otherwise
*/
int execute_internal_command(char** args){
	//execute cd command
	if (strcmp(args[0], "cd") == 0){
		if (chdir(args[1]) < 0){ //failed
			printf("Shell: Incorrect command\n");
		}
		return 1;
	}
	return 0;
}

/* Create and execute given process based on specified mode
*  return 1 if normal
*  return 0 if the process is terminated by an interrupt
*/
int execute_process(process *process_to_execute, int foreground, int parallel){

	//if the mode of execution is foreground and the command is internal, done
	if (foreground && execute_internal_command(process_to_execute -> args)) return 1;

	int c_pid = fork();

	if (c_pid > 0){
		/*BEGIN: PREPARE FOR CHILD EXECUTION*/
		//set background process in its own process group and print a message
		if (!foreground){
			setpgid(c_pid, c_pid);
			printf("Shell: Background process running (pid: %d)\n", c_pid);
		}
		process_to_execute -> pid = c_pid;
		process_to_execute -> foreground = foreground;
		/*END: PREPARE FOR CHILD EXECUTION*/

		/*BEGIN: AFTER CHILD TERMINATED*/
		if (foreground && !parallel){
            int status;
			if (waitpid(c_pid, &status, 0) == c_pid){
				process_to_execute -> terminated = 1;
                if (status == CLD_KILLED) return 0;
                return 1;
			}
		}
        return 1;
		/*END: AFTER CHILD TERMINATED*/
	} else if (c_pid == 0){
		//set background process in its own process group
		if (!foreground) setpgid(0, 0);
		process_to_execute -> pid = getpid();
		process_to_execute -> foreground = foreground;

		if (!foreground && execute_internal_command(process_to_execute -> args)) return 1;

		if (execvp(*(process_to_execute -> args), process_to_execute -> args) == -1){
			printf("Shell: Incorrect command\n");
			exit(1);
		}
        //should not execute this command
        printf("Error: this should not be seen\n");
        return 0;
	} else{
		fprintf(stderr, "Fork Failed\n");
        return 0;
	}
}

void execute_normal_foreground(job_list *jobs){
	process *cur;

	for (cur = jobs -> first; cur != NULL; cur = cur -> next){
        //if the child process is killed by signal, then skip executing further processes
		if (!execute_process(cur, 1, 0)) break;
	}

}

void execute_parallel_foreground(job_list *jobs){
	process *cur;

	//execute all process one by one
	for (cur = jobs -> first; cur != NULL; cur = cur -> next){
		execute_process(cur, 1, 1);
	}

	//wait for all process to die
	for (cur = jobs -> first; cur != NULL; cur = cur -> next){
		if (waitpid(cur -> pid, NULL, 0) > 0){
			cur -> terminated = 1;
		}
	}

}

void execute_single_background(job_list *jobs){
	process *cur;

	cur = jobs -> first;
	assert(cur -> next == NULL);

	execute_process(cur, 0, 0);
}

/* Check all background process and see if any of them finished, if so
* reap the process, if not, immediately return
*/
void check_background_job(job_list *jobs){
	job_list *cur = jobs;
	process *cur_process;
	while (cur != NULL){
		cur_process = cur -> first;
		while (cur_process != NULL){
			if (!cur_process -> foreground){
				if (waitpid(cur_process -> pid, NULL, WNOHANG) > 0){
					cur_process -> terminated = 1;
					printf("Shell: Background process finished (pid: %d)\n", cur_process -> pid);
				}
			}
			cur_process = cur_process -> next;
		}
		cur = cur -> next;
	}
}

/* Called when user types "exit", search for all unfinished background job,
* terminate it, reaps its remnant and cleanly return from the program.
*
*/
void kill_background_job(job_list *jobs){
	job_list *cur = jobs;
	process *cur_process;
	while (cur != NULL){
		cur_process = cur -> first;
		while (cur_process != NULL){
			if (!cur_process -> foreground && !cur_process -> terminated){
				//printf("%d\n", cur_process -> pid);
				if (kill(cur_process -> pid, SIGKILL) == -1){
					printf("Error_kill\n");
				}
				if (waitpid(cur_process -> pid, NULL, 0) > 0){
					cur_process -> terminated = 1;
				}
			}
			cur_process = cur_process -> next;
		}
		cur = cur -> next;
	}
}

void sig_int_handler(){
	//idle function, do nothing
    printf("\n");
}

int main(int argc, char* argv[]) {
	char  line[MAX_INPUT_SIZE];            
	char  **tokens;              
	int i;
	job_list *jobs = NULL;

	signal(SIGINT, sig_int_handler);

	FILE* fp;
	if(argc == 2) {
		fp = fopen(argv[1],"r");
		if(fp < 0) {
			printf("File doesn't exists.");
			return -1;
		}
	}

	while(1) {			
		/*BEGIN: REAP BACKGROUND PROCESS, IF ANY*/
		check_background_job(jobs);
		/*END: REAP BACKGROUND PROCESS, IF ANY*/

		/* BEGIN: TAKING INPUT */
		bzero(line, sizeof(line));
		if(argc == 2) { // batch mode
			if(fgets(line, sizeof(line), fp) == NULL) { // file reading finished
				break;	
			}
			line[strlen(line)] = '\0';
		} else { // interactive mode
			printf("$ ");
			scanf("%[^\n]", line);
			getchar();
		}
		/* END: TAKING INPUT */

		/* BEGIN: PROLOGUE -- PREPARE DATA STRUCTURE OF JOBS*/
		line[strlen(line)] = '\n'; //terminate with new line
		tokens = tokenize(line);
		job_list *latest_job = get_new_job(tokens);
		if (latest_job == NULL) continue;
		add_next_job(&jobs, latest_job);
		/* END: PROLOGUE -- PREPARE DATA STRUCTURE OF JOBS*/

		/* BEGIN: HANDLE EXIT COMMAND*/
		if (*tokens != NULL && strcmp(*tokens, "exit") == 0){
			kill_background_job(jobs);
			for(i=0;tokens[i]!=NULL;i++){
				free(tokens[i]);
			}
			free(tokens);
			break;
		}
		/* END: HANDLE EXIT COMMAND*/

		/* BEGIN: HANDLE PROCESS EXECUTION*/
		switch(get_mode_of_execution(tokens)){
			case SINGLE_FOREGROUND:
			case SERIAL_FOREGROUND:
				execute_normal_foreground(jobs);
				break;
			case SINGLE_BACKGROUND:
				execute_single_background(jobs);
				break;
			case PARALLEL_FOREGROUND:
				execute_parallel_foreground(jobs);
				break;
		}
		/* END: HANDLE PROCESS EXECUTION*/
		
     	/* BEGIN: EPILOGUE -- CLEAN UP HEAP*/
     	clean_up_process(jobs -> first);
		for(i=0;tokens[i]!=NULL;i++){
			free(tokens[i]);
		}
		free(tokens);
		//clean_up_job_list(jobs);
		/* END: EPILOGUE -- CLEAN UP HEAP*/

	}
	//before leaving the loop by exit, do the final clean up
	clean_up_process(jobs -> first);
	clean_up_job_list(jobs);
	return 0;
}
