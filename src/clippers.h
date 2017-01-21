#pragma once

#include <map>
#include <memory>
#include "tensor_operators.h"

namespace marian {

// @TODO: modify computation graph to group all paramters in single matrix object.
// This will allow to perform a single large SGD update per batch. Currently there
// are as many updates as different parameters.

class ClipperBase {
  public:
    virtual void clip(Tensor) = 0;
};

typedef std::shared_ptr<ClipperBase> ClipperPtr;

class Elementwise : public ClipperBase {
  public:
    Elementwise(float c=10.0) : c_(c) {}

    void clip(Tensor t) {
      Element(_1 = Clip(_1, c_), t);
    }

  private:
    float c_;
};

class Norm : public ClipperBase {
  public:
    Norm(float c=1.0) : c_(c) {}

    void clip(Tensor t) {
      float l2Norm = L2Norm(t);
      if(l2Norm >= c_)
        Element(_1 = (c_ / l2Norm) * _1, t);
    }

  private:
    float c_;
};


template <class Algorithm, typename ...Args>
ClipperBasePtr Clipper(Args&& ...args) {
  return ClipperBasePtr(new Algorithm(args...));
}

}
