#include  <stdio.h>
#include  <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>

int main(void){

	int cpid = fork();

	if (cpid){

		setpgid(cpid, cpid);
		printf("Give foreground to child process\n");
		tcsetpgrp(STDIN_FILENO, cpid);
		wait(NULL);

		int t = tcsetpgrp(STDIN_FILENO, getpid());

		printf("%d\n", t);

		printf("pid: %d\n", getpid());
		printf("pgid: %d\n", getpgid(0));
		printf("foreground pid: %d\n", tcgetpgrp(STDIN_FILENO));

		int b;

		scanf("%d", &b);

		printf("On the foreground again\n");

		while(1){}

	} else{

		setpgid(0, 0);
		tcsetpgrp(STDIN_FILENO, cpid);

		int a;
		scanf("%d", &a);

		printf("entered: %d\n", a);

		exit(0);

	}


}