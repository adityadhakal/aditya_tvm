/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file rpc_server.cc
 * \brief RPC Server implementation.
 */
//#define __linux__

#include <tvm/runtime/registry.h>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/select.h>
#include <sys/wait.h>
#endif
#include <set>
#include <iostream>
#include <future>
#include <thread>
#include <chrono>
#include <string>

#include <cuda_runtime.h>

#include "rpc_server.h"
#include "rpc_env.h"
#include "rpc_tracker_client.h"
#include "../../src/runtime/rpc/rpc_session.h"
#include "../../src/runtime/rpc/rpc_socket_impl.h"
#include "../../src/common/socket.h"

namespace tvm {
namespace runtime {

/*!
 * \brief wait the child process end.
 * \param status status value
 */
#if defined(__linux__) || defined(__ANDROID__)
__attribute__((unused)) static pid_t waitPidEintr(int *status) {
  pid_t pid = 0;
  while ((pid = waitpid(-1, status, 0)) == -1) {
    if (errno == EINTR) {
      continue;
    } else {
      perror("waitpid");
      abort();
    }
  }
  return pid;
}
#endif

/*!
 * \brief RPCServer RPC Server class.
 * \param host The hostname of the server, Default=0.0.0.0
 * \param port The port of the RPC, Default=9090
 * \param port_end The end search port of the RPC, Default=9199
 * \param tracker The address of RPC tracker in host:port format e.g. 10.77.1.234:9190 Default=""
 * \param key The key used to identify the device type in tracker. Default=""
 * \param custom_addr Custom IP Address to Report to RPC Tracker. Default=""
 */
class RPCServer {
 public:
  /*!
   * \brief Constructor.
  */
  RPCServer(const std::string &host,
            int port,
            int port_end,
            const std::string &tracker_addr,
            const std::string &key,
            const std::string &custom_addr) {
    // Init the values
    host_ = host;
    port_ = port;
    port_end_ = port_end;
    tracker_addr_ = tracker_addr;
    key_ = key;
    custom_addr_ = custom_addr;
    //zero the count instances variable
    count_instances = 0;
    gpu_reset_count = 0;
  }

  /*!
   * \brief Destructor.
  */
  ~RPCServer() {
    // Free the resources
    tracker_sock_.Close();
    listen_sock_.Close();
  }

  /*!
   * \brief Start Creates the RPC listen process and execution.
  */
  void Start() {
	  /* find the PID of MPS control and server */
	  //we assume MPS control process is already alive
	  pid_t mps_control_pid, mps_server_pid;
	  char mps_control_pid_s[1024];
	  char mps_server_pid_s[1024];
	  FILE *cmd = popen("pidof nvidia-cuda-mps-control", "r");
	  fgets(mps_control_pid_s, 1024, cmd);
	  mps_control_pid = strtoul(mps_control_pid_s, NULL, 10);
	  pclose(cmd);
	  LOG(INFO)<<"PID of NVIDIA-CUDA-MPS-CONTROL is "<<mps_control_pid;

	  FILE *cmd2 = popen("pidof nvidia-cuda-mps-server", "r");
	  fgets(mps_server_pid_s, 1024, cmd2);
	  mps_server_pid = strtoul(mps_server_pid_s, NULL, 10);
	  pclose(cmd2);

	  if(mps_server_pid == 0){
		  memset(mps_server_pid_s,0,1024);
		  int status = system("echo \"start_server -uid 0\" | nvidia-cuda-mps-control");
		  printf("Status is %d \n",status);
		  cmd2 = popen("pidof nvidia-cuda-mps-server", "r");
		  fgets(mps_server_pid_s, 1024, cmd2);
		  mps_server_pid = strtoul(mps_server_pid_s, NULL, 10);
		  pclose(cmd2);
		  printf("[CHILD] PID of the MPS server is %s\n", mps_server_pid_s);

	  }else
	  {
		  LOG(INFO)<<"[Parent] PID of the MPS server is "<<mps_server_pid;
	  }
	  sleep(1);
	  pid_t worker1_pid = fork();
	  if (worker1_pid == 0) {
		  LOG(INFO)<<"Creating Worker 1";
		  // Worker process
		  listen_sock_.Create();
		  my_port_ = listen_sock_.TryBindHost(host_, port_, port_end_);
		  LOG(INFO) << "bind to " << host_ << ":" << my_port_;
		  listen_sock_.Listen(1);
		  /* why don't we rather fork the process and wait for the result */
		  std::future<void> proc(std::async(std::launch::async, &RPCServer::ListenLoopProc, this));
		  proc.get();

		  // Close the listen socket
		  listen_sock_.Close();

		  //exit the fork:prevent runaway fork
		  _exit(0);
	  }
	  pid_t worker2_pid = fork();
	  if (worker2_pid == 0) {
		  LOG(INFO)<<"Creating Worker 2";
		  // Worker process
		  listen_sock_.Create();
		  my_port_ = listen_sock_.TryBindHost(host_, port_, port_end_);
		  LOG(INFO) << "bind to " << host_ << ":" << my_port_;
		  listen_sock_.Listen(1);
		  /* why don't we rather fork the process and wait for the result */
		  std::future<void> proc(std::async(std::launch::async, &RPCServer::ListenLoopProc, this));
		  proc.get();

		  // Close the listen socket
		  listen_sock_.Close();
		  //exit the fork:prevent runaway fork
		  _exit(0);
	  }

	  // Fork a process to just check MPS server is alive
	  pid_t service_checker = fork();
	  if(service_checker == 0){
		  //check MPS server status every second
		  while(true){
			  sleep(1);
			  int down_services = 0;

			  if(kill(mps_server_pid,0)!=0){
				  //mps server is dead
				  LOG(INFO)<<"MPS Server "<<mps_server_pid<<"is Dead.";
				  down_services++;
			  }
			  /*
			  if(kill(worker1_pid, 0)!=0){
				  //worker 1 is dead
				  LOG(INFO)<<"Worker 1 is dead "<<worker1_pid;
				  down_services++;
			  }
			  if(kill(worker2_pid,0)!=0){
				  //worker 2 is dead
				  LOG(INFO)<<"Worker 2 is dead "<<worker2_pid;
				  down_services++;
			  }
			   */
			  //LOG(INFO)<<down_services<<" Processes Dead";
			  if(down_services){
				  LOG(INFO)<<"Killing the service checker ***";

				  exit(0);//let the parent know something is down.
			  }

		  }
		  //prevent runaway forking
		  _exit(0);
	  }


	  while(true){
		  sleep(1);
		  int status = 0;
		  //wait for the child process
		  pid_t exiting_pid = waitpid(service_checker,&status,WNOHANG);
		  //printf("Exit Status of the Service checker was: %d",status);
		  if(exiting_pid == service_checker){
			  //check if MPS server is dead
			  if(kill(mps_server_pid,0)!=0){
				  //mps server is dead
				  LOG(INFO)<<"MPS Server "<<mps_server_pid<<"is Dead.";
				  //relaunch it. .. relaunching is quite tricky.. needs to kill all worker nodes and then kill MPS control and relaunch MPS control
				  //here we need to reset the MPS control deamon as well as kill the existing process and start them again.
				  LOG(INFO)<<"MPS SERVER DIED.... RESETTING IT";
				  kill(mps_control_pid,9);
				  kill(worker1_pid, 9);
				  //wait(&status);
				  kill(worker2_pid, 9);
				  //wait(&status);
				  //start another MPS control
				  pid_t pid_mps_control = fork();
				  if(pid_mps_control == 0){
					  /* child process */
					  static char *argv[] = {"nvidia-cuda-mps-control","-d", NULL};
					  execv("/usr/bin/nvidia-cuda-mps-control", argv);
					  exit(127);
				  }
				  sleep(1); //let the control deamon start
				  memset(mps_control_pid_s,0,1024);
				  cmd = popen("pidof nvidia-cuda-mps-control", "r");
				  fgets(mps_control_pid_s, 1024, cmd);
				  mps_control_pid = strtoul(mps_control_pid_s, NULL, 10);
				  pclose(cmd);
				  LOG(INFO)<<"PID of Restarted NVIDIA-CUDA-MPS-CONTROL is "<<mps_control_pid;

				  //start an MPS server
				  memset(mps_server_pid_s,0,1024);
				  status = system("echo \"start_server -uid 0\" | nvidia-cuda-mps-control");
				  printf("System Status is %d \n",status);
				  cmd2 = popen("pidof nvidia-cuda-mps-server", "r");
				  fgets(mps_server_pid_s, 1024, cmd2);
				  mps_server_pid = strtoul(mps_server_pid_s, NULL, 10);
				  pclose(cmd2);
				  printf("PID of the MPS server is %s [inside while(true)]\n", mps_server_pid_s);
			  }
			  // Fork a process to check MPS server is alive
			  service_checker = fork();
			  if(service_checker == 0){
				  //check MPS server status every second
				  while(true){
					  sleep(1);
					  int down_services = 0;

					  if(kill(mps_server_pid,0)!=0){
						  //mps server is dead
						  LOG(INFO)<<"MPS Server "<<mps_server_pid<<"is Dead.";
						  down_services++;
					  }
					  //LOG(INFO)<<down_services<<" Processes Dead";
					  if(down_services)
						  exit(0);//let the parent know something is down.
				  }
				  //prevent runaway forking
				  _exit(0);
			  }
		  }

		  /* wait and check if worker 2 is dead */
		  pid_t check_worker_2 = waitpid(worker2_pid, &status, WNOHANG);
		  if(check_worker_2 == worker2_pid || (kill(worker2_pid,0)!=0)){
			  //if the worker2 process have died
			  worker2_pid = fork();
			  if (worker2_pid == 0) {
				  LOG(INFO)<<"RE-Launching Worker 2";
				  // Worker process
				  listen_sock_.Create();
				  my_port_ = listen_sock_.TryBindHost(host_, port_, port_end_);
				  LOG(INFO) << "bind to " << host_ << ":" << my_port_;
				  listen_sock_.Listen(1);
				  /* why don't we rather fork the process and wait for the result */
				  std::future<void> proc(std::async(std::launch::async, &RPCServer::ListenLoopProc, this));
				  proc.get();

				  // Close the listen socket
				  listen_sock_.Close();
				  //prevent runaway forking
				  _exit(0);
			  }
		  }
		  /* wait and see if worker 1 is dead */
		  pid_t check_worker_1 = waitpid(worker1_pid, &status,WNOHANG);
		  if(worker1_pid == check_worker_1 || (kill(worker1_pid,0)!=0)){
			  worker1_pid = fork();
			  if (worker1_pid == 0) {
				  LOG(INFO)<<"RE-Launching Worker 1";
				  // Worker process
				  listen_sock_.Create();
				  my_port_ = listen_sock_.TryBindHost(host_, port_, port_end_);
				  LOG(INFO) << "bind to " << host_ << ":" << my_port_;
				  listen_sock_.Listen(1);
				  /* why don't we rather fork the process and wait for the result */
				  std::future<void> proc(std::async(std::launch::async, &RPCServer::ListenLoopProc, this));
				  proc.get();

				  // Close the listen socket
				  listen_sock_.Close();
				  //prevent runaway forking
				  _exit(0);
			  }

		  }

	  }
  }

 private:
  int count_instances; //private variable to count instances
  int gpu_reset_count; //counting number of time GPU is reset
  /*!
   * \brief ListenLoopProc The listen process.
   */
  void ListenLoopProc() {
    TrackerClient tracker(tracker_addr_, key_, custom_addr_);
    while (true) {
      common::TCPSocket conn;
      common::SockAddr addr("0.0.0.0", 0);
      std::string opts;
      try {
        // step 1: setup tracker and report to tracker
        tracker.TryConnect();
        // step 2: wait for in-coming connections
        AcceptConnection(&tracker, &conn, &addr, &opts);
      }
      catch (const char* msg) {
        LOG(WARNING) << "Socket exception: " << msg;
        // close tracker resource
        tracker.Close();
        continue;
      }
      catch (std::exception& e) {
        // Other errors
        LOG(WARNING) << "Exception standard: " << e.what();
        continue;
      }


      __attribute__((unused)) int timeout = GetTimeOutFromOpts(opts);
      #if defined(__linux__) || defined(__ANDROID__)

      //Aditya's code
      int num_sms;
      cudaDeviceGetAttribute(&num_sms,cudaDevAttrMultiProcessorCount,0);
      size_t gpu_free, total_gpu, gpu_occupied;
      cudaError_t errval = cudaMemGetInfo(&gpu_free, &total_gpu);
      if (errval != cudaSuccess) {
    	  LOG(INFO)<<"Error Getting GPU Mem. Error: "<<errval<<" Restarting with another process";
    	  LOG(INFO) << "Socket Connection Closed";
    	  conn.Close();
    	  exit(0);
      }
      gpu_occupied = total_gpu - gpu_free;
      LOG(INFO)<<"GPU Memory: Total: "<<(total_gpu/(1024*1024*1024))<<" Free: "<<(gpu_free/(1024*1024*1024))<<" Occupied Mem: "<<(gpu_occupied/(1024*1024*1024))<<" Time GPU is Reset: "<<gpu_reset_count;
      if (gpu_occupied > total_gpu / 4) {
    	  gpu_reset_count++;
    	  LOG(INFO)<<"More than quarter of GPU memory exceeded. Resetting GPU context. Number of time context is reset: "<<gpu_reset_count;
    	  cudaDeviceReset();
    	  //now wait till other process have also freed the memory
    	  int counter = 0;
    	  while(gpu_occupied> total_gpu/4){
    		 errval = cudaMemGetInfo(&gpu_free, &total_gpu);
    		 gpu_occupied = total_gpu-gpu_free;
    		 counter++;
    		 if(counter >10000){
    			 cudaDeviceReset();
    		 }
    	  }
      }

      count_instances++;
      ServerLoopProc(conn,addr);
      cudaError_t last_error = cudaGetLastError();
      LOG(INFO)<<"Last CUDA ERROR: "<<last_error;
      if (last_error != cudaSuccess) {
    	  LOG(INFO)<<"Restarting the process due to GPU error: ";
    	  LOG(INFO) << "Socket Connection Closed";
    	  conn.Close();
    	  exit(0);
      }
      LOG(INFO) <<"Counting the number of instances: "<<count_instances <<" Multiprocessor count: "<<num_sms;

      //similarly check if GPU memory occupancy is high. Reset if it is.
      errval = cudaMemGetInfo(&gpu_free, &total_gpu);
      gpu_occupied = total_gpu-gpu_free;
      if (gpu_occupied > total_gpu / 4) {
    	  gpu_reset_count++;
    	  LOG(INFO)<<"More than quarter of GPU memory exceeded. Resetting GPU context. Number of time context is reset: "<<gpu_reset_count;
    	  cudaDeviceReset();
      }
      
      //if(count_instances%50 ==0 )
      //	cudaDeviceReset();
      //CODE CHANGED TO REMOVE FORKING REQUIREMENT
      /*
        // step 3: serving
        if (timeout != 0) {
          const pid_t timer_pid = fork();
          if (timer_pid == 0) {
            // Timer process
            sleep(timeout);
            exit(0);
          }

          const pid_t worker_pid = fork();
          if (worker_pid == 0) {
            // Worker process
            ServerLoopProc(conn, addr);
            exit(0);
          }

          int status = 0;
          const pid_t finished_first = waitPidEintr(&status);
          if (finished_first == timer_pid) {
            kill(worker_pid, SIGKILL);
          } else if (finished_first == worker_pid) {
            kill(timer_pid, SIGKILL);
          } else {
            LOG(INFO) << "Child pid=" << finished_first << " unexpected, but still continue.";
          }

          int status_second = 0;
          waitPidEintr(&status_second);

          // Logging.
          if (finished_first == timer_pid) {
            LOG(INFO) << "Child pid=" << worker_pid << " killed (timeout = " << timeout
                      << "), Process status = " << status_second;
          } else if (finished_first == worker_pid) {
            LOG(INFO) << "Child pid=" << timer_pid << " killed, Process status = " << status_second;
          }
        } else {
          auto pid = fork();
          if (pid == 0) {
            ServerLoopProc(conn, addr);
            exit(0);
          }
          // Wait for the result
          int status = 0;
          wait(&status);
          LOG(INFO) << "Child pid=" << pid << " exited, Process status =" << status;

        }
        */
      #else
        // step 3: serving
        std::future<void> proc(std::async(std::launch::async,
                                          &RPCServer::ServerLoopProc, this, conn, addr));
        // wait until server process finish or timeout
        if (timeout != 0) {
          // Autoterminate after timeout
          proc.wait_for(std::chrono::seconds(timeout));
        } else {
          // Wait for the result
          proc.get();
        }
      #endif
      // close from our side.
      LOG(INFO) << "Socket Connection Closed";
      conn.Close();
    }
  }


  /*!
   * \brief AcceptConnection Accepts the RPC Server connection.
   * \param tracker Tracker details.
   * \param conn New connection information.
   * \param addr New connection address information.
   * \param opts Parsed options for socket
   * \param ping_period Timeout for select call waiting
   */
  void AcceptConnection(TrackerClient* tracker,
                        common::TCPSocket* conn_sock,
                        common::SockAddr* addr,
                        std::string* opts,
                        int ping_period = 2) {
    std::set <std::string> old_keyset;
    std::string matchkey;

    // Report resource to tracker and get key
    tracker->ReportResourceAndGetKey(my_port_, &matchkey);

    while (true) {
      tracker->WaitConnectionAndUpdateKey(listen_sock_, my_port_, ping_period, &matchkey);
      common::TCPSocket conn = listen_sock_.Accept(addr);

      int code = kRPCMagic;
      CHECK_EQ(conn.RecvAll(&code, sizeof(code)), sizeof(code));
      if (code != kRPCMagic) {
        conn.Close();
        LOG(FATAL) << "Client connected is not TVM RPC server";
        continue;
      }

      int keylen = 0;
      CHECK_EQ(conn.RecvAll(&keylen, sizeof(keylen)), sizeof(keylen));

      const char* CLIENT_HEADER = "client:";
      const char* SERVER_HEADER = "server:";
      std::string expect_header = CLIENT_HEADER + matchkey;
      std::string server_key = SERVER_HEADER + key_;
      if (size_t(keylen) < expect_header.length()) {
        conn.Close();
        LOG(INFO) << "Wrong client header length";
        continue;
      }

      CHECK_NE(keylen, 0);
      std::string remote_key;
      remote_key.resize(keylen);
      CHECK_EQ(conn.RecvAll(&remote_key[0], keylen), keylen);

      std::stringstream ssin(remote_key);
      std::string arg0;
      ssin >> arg0;
      if (arg0 != expect_header) {
          code = kRPCMismatch;
          CHECK_EQ(conn.SendAll(&code, sizeof(code)), sizeof(code));
          conn.Close();
          LOG(WARNING) << "Mismatch key from" << addr->AsString();
          continue;
      } else {
        code = kRPCSuccess;
        CHECK_EQ(conn.SendAll(&code, sizeof(code)), sizeof(code));
        keylen = server_key.length();
        CHECK_EQ(conn.SendAll(&keylen, sizeof(keylen)), sizeof(keylen));
        CHECK_EQ(conn.SendAll(server_key.c_str(), keylen), keylen);
        LOG(INFO) << "Connection success " << addr->AsString();
        ssin >> *opts;
        *conn_sock = conn;
        return;
      }
    }
  }

  /*!
   * \brief ServerLoopProc The Server loop process.
   * \param sock The socket information
   * \param addr The socket address information
   */
  void ServerLoopProc(common::TCPSocket sock, common::SockAddr addr) {
      // Server loop
      auto env = RPCEnv();
      RPCServerLoop(sock.sockfd);
      LOG(INFO) << "Finish serving " << addr.AsString();
      env.CleanUp();
  }

  /*!
   * \brief GetTimeOutFromOpts Parse and get the timeout option.
   * \param opts The option string
   * \param timeout value after parsing.
   */
  int GetTimeOutFromOpts(std::string opts) {
    std::string cmd;
    std::string option = "-timeout=";

    if (opts.find(option) == 0) {
      cmd = opts.substr(opts.find_last_of(option) + 1);
      CHECK(common::IsNumber(cmd)) << "Timeout is not valid";
      return std::stoi(cmd);
    }
    return 0;
  }

  std::string host_;
  int port_;
  int my_port_;
  int port_end_;
  std::string tracker_addr_;
  std::string key_;
  std::string custom_addr_;
  common::TCPSocket listen_sock_;
  common::TCPSocket tracker_sock_;
};

/*!
 * \brief RPCServerCreate Creates the RPC Server.
 * \param host The hostname of the server, Default=0.0.0.0
 * \param port The port of the RPC, Default=9090
 * \param port_end The end search port of the RPC, Default=9199
 * \param tracker The address of RPC tracker in host:port format e.g. 10.77.1.234:9190 Default=""
 * \param key The key used to identify the device type in tracker. Default=""
 * \param custom_addr Custom IP Address to Report to RPC Tracker. Default=""
 * \param silent Whether run in silent mode. Default=True
 */
void RPCServerCreate(std::string host,
                     int port,
                     int port_end,
                     std::string tracker_addr,
                     std::string key,
                     std::string custom_addr,
                     bool silent) {
  if (silent) {
    // Only errors and fatal is logged
    dmlc::InitLogging("--minloglevel=2");
  }
  // Start the rpc server
  RPCServer rpc(host, port, port_end, tracker_addr, key, custom_addr);
  rpc.Start();
}

TVM_REGISTER_GLOBAL("rpc._ServerCreate")
.set_body([](TVMArgs args, TVMRetValue* rv) {
    RPCServerCreate(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
  });
}  // namespace runtime
}  // namespace tvm
