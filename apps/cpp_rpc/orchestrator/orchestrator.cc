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

				/*move directory for the process*/
				//int status = chdir("/home/adhak001/dev/tvm_v06/apps/cpp_rpc/orchestrator/work_directory1");
				//if(status != 0)
				//	printf("Failed to Change directory.\n");

				//Command Line arguments for excve
				/*
	   char *const paramlist[] = {"./tvm_rpc", "server", "--tracker=192.168.0.90:9190", "--key=aditya",NULL};
		char *const envlist[] = {"CUDA_MPS_ACTIVE_THREAD_PERCENTAGE=50",NULL};

		execve("./tvm_rpc", paramlist, envlist);
		printf("Return not expected. Must be an execve error.n");
		//_exit(0);
				 */
				//system("./tvm_rpc server --tracker=192.168.0.90:9190 --key=aditya");
				system("/home/adhak001/dev/tvm_v06/apps/cpp_rpc/tvm_rpc server --tracker=192.168.0.90:9190 --key=aditya");
				_exit(0);
			}
		}
		if(proc2){
			proc2 = 0;
			//second fork
			child_pid2 = fork();
			if(child_pid2==0){
				//second child
				system("/home/adhak001/dev/tvm_v06/apps/cpp_rpc/tvm_rpc server --tracker=192.168.0.90:9190 --key=aditya");
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

		//printf("Child process died. ");
		//std::cout<<"Restarting the Tuning Process"<<std::endl;
		sleep(1);
	}
	return 0;
}



