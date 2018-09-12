#include <posix/pipe_fd.hpp>

ssize_t PipeBuffer::read(char *dst,ssize_t length,bool blocking)
{

  if (!blocking)
  {
    if (R.empty())
      return -EAGAIN;
    return R.read(dst,length);
  }


  while(R.empty()); //busy wait for data.. this is not nice

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
        printf("stderr read end not open\n");
        //Singal Sigpipe if ignored raise EPIPE
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
            printf("writing to file\n");
            int res=R.write(&src[size],length);
            printf("Wrote %d bytes to file\n",res);
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
