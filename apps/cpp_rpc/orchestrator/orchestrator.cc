/*
 * orchestrator.cc
 *
 *  Created on: Apr 18, 2020
 *      Author: Aditya
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <iostream>
#include<stdlib.h>
int main(void) {
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) != NULL) {
		printf("Current working dir: %s\n", cwd);
	} else {
		perror("getcwd() error");
		return 1;
	}

	int proc1,proc2;
	proc1 = 1;//flag for proc 1; let it spawn the process first time
	proc2 = 1; //flag for proc 2; let it spawn the process first time
	pid_t child_pid, child_pid2;
	while(true){

		if(proc1){
			proc1 = 0;
			//fork the process first and run execve
			child_pid = fork();
			if(child_pid==0){
				//child process
				system("/home/adhak001/dev_tvm/aditya_tvm/apps/cpp_rpc/tvm_rpc server --tracker=192.168.0.90:9190 --key=aditya");
				_exit(0);
			}
		}
		//if 2nd process should be started/restarted
		if(proc2){
			proc2 = 0;
			//second fork
			child_pid2 = fork();
			if(child_pid2==0){
				//second child
				system("/home/adhak001/dev_tvm/aditya_tvm/apps/cpp_rpc/tvm_rpc server --tracker=192.168.0.90:9190 --key=aditya");
				_exit(0);
			}
		}
		int status = 0;
		//wait(&status);
		pid_t return_pid1, return_pid2;
		return_pid1 = waitpid(child_pid,&status,WNOHANG);
		if(return_pid1 == child_pid){
			std::cout<<"Proc 1 has died, restarting it"<<std::endl;
			proc1 = 1;
		}
		return_pid2 = waitpid(child_pid2,&status,WNOHANG);
		if(return_pid2 == child_pid2)
		{
			std::cout<<"Proc 2 has died, restarting it"<<std::endl;
			proc2 = 1;
		}
		usleep(100000); //100 ms sleep
	}
	return 0;
}



