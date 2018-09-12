#ifndef PIPE_FD_HPP
#define PIPE_FD_HPP

#include "posix/fd.hpp"
#include <ringbuffer>
#include <memory>

class PipeBuffer
{
public:
  void setStatusRead(bool open)
  { readOpen=open; }

  void setStatusWrite(bool open)
  { writeOpen=open; }

  ssize_t read(char *dst,ssize_t length,bool blocking);

  ssize_t write(const char *src,ssize_t length,bool blocking);

private:
  FixedRingBuffer<65535> R;
  bool writeOpen;
  bool readOpen;
};


class Pipe_read_FD : public FD {
public:
    Pipe_read_FD(int id, std::shared_ptr<PipeBuffer> &pb) : FD(id), P(pb)
    {
        P->setStatusRead(true);
    }
    ssize_t read(void *dst,size_t length) override
    {
        return P->read((char*)dst,length,is_blocking());
    }
    int close() override {
        P->setStatusRead(false);
        return 0;
    }
private:
    std::shared_ptr<PipeBuffer> P;
};

class Pipe_write_FD : public FD {
public:
    Pipe_write_FD(int id, std::shared_ptr<PipeBuffer> pb) : FD(id) , P(pb)
    {
        P->setStatusWrite(true);
    }

    int write(const void *src,size_t length) override
    {
        return P->write((const char*)src,length,is_blocking());
    }
    int close() override
    {
      P->setStatusWrite(false);
      return 0;
    }
private:
    std::shared_ptr<PipeBuffer> P;
};


#endif // PIPE_FD_HPP
