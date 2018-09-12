#include "common.hpp"
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
    //get fd from fd_map (inneficient..)
 //   if(auto* fildes = FD_map::_get(pipefd[0]); fildes)
 //     return fildes->fcntl(F_SETFD,flags);
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
