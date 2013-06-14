#pragma once

#include <bulk/detail/config.hpp>
#include <bulk/algorithm/copy.hpp>
#include <bulk/malloc.hpp>
#include <thrust/iterator/iterator_traits.h>
#include <thrust/detail/minmax.h>


BULK_NS_PREFIX
namespace bulk
{
namespace detail
{
namespace reduce_detail
{


template<typename ExecutionGroup, typename RandomAccessIterator, typename Size, typename T, typename BinaryFunction>
__device__ T destructive_reduce_n(ExecutionGroup &g, RandomAccessIterator first, Size n, T init, BinaryFunction binary_op)
{
  typedef int size_type;

  size_type tid = g.this_exec.index();

  Size m = n;

  while(m > 1)
  {
    Size half_m = m >> 1;

    if(tid < half_m)
    {
      T old_val = first[tid];

      first[tid] = binary_op(old_val, first[m - tid - 1]);
    } // end if

    g.wait();

    m -= half_m;
  } // end while

  g.wait();

  T result = (n > 0) ? binary_op(init,first[0]) : init;

  g.wait();

  return result;
}


} // end reduce_detail
} // end detail


template<std::size_t groupsize, std::size_t grainsize, typename RandomAccessIterator, typename T, typename BinaryFunction>
__device__
T reduce(bulk::static_execution_group<groupsize,grainsize> &g,
         RandomAccessIterator first,
         RandomAccessIterator last,
         T init,
         BinaryFunction binary_op)
{
  typedef int size_type;

  const size_type elements_per_group = groupsize * grainsize;

  size_type tid = g.this_exec.index();

  T this_sum;

  bool this_sum_defined = false;

  typename thrust::iterator_difference<RandomAccessIterator>::type n = last - first;

#if __CUDA_ARCH__ >= 200
  T *buffer = reinterpret_cast<T*>(bulk::malloc(g, groupsize * sizeof(T)));
#else
  __shared__ thrust::system::cuda::detail::detail::uninitialized_array<T,groupsize> buffer_impl;
  T *buffer = buffer_impl.data();
#endif
  
  for(; first < last; first += elements_per_group)
  {
    size_type partition_size = thrust::min<size_type>(elements_per_group, last - first);
    
    // load input into register
    T inputs[grainsize];

    // XXX this is a sequential strided copy
    //     the stride is groupsize
    if(partition_size >= elements_per_group)
    {
      for(size_type i = 0; i < grainsize; ++i)
      {
        inputs[i] = first[groupsize * i + tid];
      } // end for
    } // end if
    else
    {
      for(size_type i = 0; i < grainsize; ++i)
      {
        size_type index = groupsize * i + tid;
        if(index < partition_size)
        {
          inputs[i] = first[index];
        } // end if
      } // end for
    } // end else
    
    // sum sequentially
    for(size_type i = 0; i < grainsize; ++i)
    {
      size_type index = groupsize * i + g.this_exec.index();
      if(index < partition_size)
      {
        T x = inputs[i];
        this_sum = (i || this_sum_defined) ? binary_op(this_sum, x) : x;
      } // end if
    } // end for

    this_sum_defined = true;
  } // end for

  if(this_sum_defined)
  {
    buffer[tid] = this_sum;
  } // end if

  g.wait();

  // reduce across the block
  T result = detail::reduce_detail::destructive_reduce_n(g, buffer, thrust::min<size_type>(groupsize,n), init, binary_op);

#if __CUDA_ARCH__ >= 200
  bulk::free(g,buffer);
#endif

  return result;
} // end reduce


} // end bulk
BULK_NS_SUFFIX

