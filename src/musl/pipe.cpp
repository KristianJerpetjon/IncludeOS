﻿#include "common.hpp"
#include <posix/fd_map.hpp>
#include <posix/pipe_fd.hpp>

static long sys_pipe([[maybe_unused]]int pipefd[2])
{
  auto a = std::make_shared<PipeBuffer>();
  auto rdfdes = FD_map::_open<Pipe_read_FD>(a);
  auto wrfdes = FD_map::_open<Pipe_write_FD>(a);

  pipefd[0]=rdfdes.get_id();
  pipefd[1]=wrfdes.get_id();

  return 0;
}

static long sys_pipe2([[maybe_unused]]int pipefd[2], [[maybe_unused]]int flags)
{
  if (sys_pipe(pipefd) == 0)
  {
    //TODO
     /*
    if ((fcntl(pipefd[0],F_SETFL,flags) != 0) || (fcntl(pipefd[1],F_SETFL,flags) != 0))
    {
      return -EFAULT;
    }*/
  }
  return -ENOSYS;
}

extern "C" {
long syscall_SYS_pipe(int pipefd[2]) {
  return strace(sys_pipe, "pipe", pipefd);
}

long syscall_SYS_pipe2(int pipefd[2], int flags) {
  return strace(sys_pipe2, "pipe2", pipefd, flags);
}
} // extern "C"
