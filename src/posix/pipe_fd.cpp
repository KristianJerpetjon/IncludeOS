#include <posix/pipe_fd.hpp>
#include <kernel/fiber.hpp>

ssize_t PipeBuffer::read(char *dst,ssize_t length,bool blocking)
{

  if (!writeOpen)
  {
      //end of file.. as write end is closed.. should we have flushed the buffer out first.. or is this UNDEF
      return 0;
  }
  if (!blocking)
  {
    if (R.empty())
      return -EAGAIN;
    return R.read(dst,length);
  }

  while(R.empty())
  {
      //is this correct ?
      //Fiber::yield();
  }

  ssize_t res=R.read(dst,length);
  if (res < 0) //or throw
  {
      return -EFAULT;
  }
  return res;
}

ssize_t PipeBuffer::write(const char *src,ssize_t length,bool blocking)
{
    ssize_t size=0;
    if (!readOpen)
    {
        perror("read end not open\n");
        //Singal Sigpipe if ignored rastderise EPIPE
        return -EPIPE;
    }
    if (!blocking)
    {
        if (R.full())
            return -EAGAIN;
        return R.write(src,length);
    }
    else
    {
        while (length > 0)
        {
            while(R.free_space() == 0)
            {
                //is this correct
                //Fiber::yield();
            }
            int res=R.write(&src[size],length);
            if (res < 0) //or throw
            {
                return -EFAULT;
            }
            size+=res;
            length-=res;
        }
    }
    return size;
}
