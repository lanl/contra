#include "config.hpp"
#include "cuda_rt.hpp"
#include "tasking_rt.hpp"

#include "cuda_utils.hpp"

#include <stdio.h>
#include <iostream>

extern "C" {

//==============================================================================
/// index space partitioning
//==============================================================================
__device__ void contra_cuda_index_space_create_from_partition(
    int_t i,
    contra_cuda_partition_t * part,
    contra_index_space_t * is
    )
{ is->setup(part->offsets[i], part->offsets[i+1]); }

//==============================================================================
/// Accessor write
//==============================================================================
__device__ void contra_cuda_accessor_write(
    contra_cuda_accessor_t * acc,
    const void * data,
    int_t index)
{
  auto pos = (acc->offsets[threadIdx.x] + index) * acc->data_size;
  byte_t * offset = static_cast<byte_t*>(acc->data) + pos;
  memcpy(offset, data, acc->data_size);
}

//==============================================================================
/// Accessor read
//==============================================================================
__device__ void contra_cuda_accessor_read(
    contra_cuda_accessor_t * acc,
    void * data,
    int_t index)
{
  auto pos = (acc->offsets[threadIdx.x] + index) * acc->data_size;
  const byte_t * offset = static_cast<const byte_t*>(acc->data) + pos;
  memcpy(data, offset, acc->data_size);
  //printf("%d accessing %ld\n", threadIdx.x, acc->offsets[threadIdx.x] + index);
}

//==============================================================================
// prepare a reduction
//==============================================================================
__device__ void contra_cuda_set_reduction_value(
  void ** indata,
  void * data,
  size_t data_size)
{
  auto pos = data_size*threadIdx.x;
  byte_t * offset = static_cast<byte_t*>(*indata) + pos;
  memcpy(offset, data, data_size);
}

} // extern

