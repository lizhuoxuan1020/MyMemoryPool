#pragma once

#include "common.h"
#include "thread_cache.h"
#include "page_heap.h"

namespace memory_pool {
	//被动调用，哪个线程来了之后，需要内存就调用这个接口
	inline static ObjPtr Allocate(size_t size) {
		//超过一个最大值 64k，就自己从系统中获取，否则使用内存池
		if (size > MAX_BYTES) {
			//return malloc(size);
			Span* span = PageHeap::GetInstance()->AllocateLargeSpan(size);
			void* ptr = (void*)(span->pageid_ << PAGE_SHIFT);
			return ptr;
		}
		else {
			//第一次来，自己创建，后面来的，就可以直接使用当前创建好的内存池
			if (tlslist == nullptr) {
				tlslist = new ThreadCache;
			}

			return tlslist->AllocateObj(size);
		}
	}

	inline static ObjPtr Alloc(size_t size) {
		return Allocate(size);
	}

	inline static void Free(void* ptr) {
		Span* span = PageHeap::GetInstance()->FindSpan(ptr);
		size_t size = span->objsize_;
		if (size > MAX_BYTES) {
			//free(ptr);
			PageHeap::GetInstance()->ReclaimLargeSpan(span);
		}
		else {
			tlslist->ReclaimObj(ptr, size);
		}
	}
} // namespace memory_pool

