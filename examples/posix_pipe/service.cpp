// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2015 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <service>
#include <cstdio>
#include <isotime>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

void Service::start(const std::string& args)
{
#ifdef __GNUG__
  printf("Built by g++ " __VERSION__ "\n");
#endif
  printf("Hello world! Time is now %s\n", isotime::now().c_str());
  
  char buff_in[256];
  char buff_out[256];
  snprintf(buff_in,256,"Hello IncludeOS demo Pipe\n");

  int pipefd[2];
  if (pipe(pipefd) != 0)
  {
	  perror("Failed to open pipe");
  }

  int res=write(pipefd[1],buff_in,strlen(buff_in)+1);
  printf("wrote %d bytes to pipe\n",res);
  res=read(pipefd[0],buff_out,256);
  printf("read %d bytes from pipe\n",res);
  printf("Received from pipe : \n\t %s\n",buff_out);
  
  printf("Closing write end");
  close(pipefd[1]); //this closes the write end file descriptor 

  res=read(pipefd[0],buff_out,256);
  if (res != 0)
  {
    printf("ERROR read with write end closed should return 0\n");
    return;
  }
  printf("closing read end as eof is received from write end\n");
  close(pipefd[0]); //should release the underlaying buffer but the use of ptr type in map..

  if (pipe2(pipefd,O_NONBLOCK) != 0)
  {
	  printf("Pipe2 failed\n");
  }

  if ( read(pipefd[0],buff_out,1) < 0)
  {
    printf("error %s\n",strerror(errno));
  }

  write(pipefd[1],buff_in,strlen(buff_in)+1);

  res=read(pipefd[0],buff_out,256);
  printf("read %d bytes from pipe\n",res);
  printf("Received from pipe nonblock: \n\t %s\n",buff_out);



/*  struct timespec ts;
  ts.tv_nsec=2000;
  ts.tv_sec=1;
  nanosleep(&ts,nullptr);*/
  printf("Hello world! Time is now %s\n", isotime::now().c_str());
  printf("Args = %s\n", args.c_str());
  printf("Try giving the service less memory, eg. 5MB in vm.json\n");
}
