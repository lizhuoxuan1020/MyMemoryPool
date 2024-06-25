#pragma once

#include "common.h"
#include "thread_cache.h"
#include "page_heap.h"

namespace memory_pool {
	//�������ã��ĸ��߳�����֮����Ҫ�ڴ�͵�������ӿ�
	inline static ObjPtr Allocate(size_t size) {
		//����һ�����ֵ 64k�����Լ���ϵͳ�л�ȡ������ʹ���ڴ��
		if (size > MAX_BYTES) {
			//return malloc(size);
			Span* span = PageHeap::GetInstance()->AllocateLargeSpan(size);
			void* ptr = (void*)(span->pageid_ << PAGE_SHIFT);
			return ptr;
		}
		else {
			//��һ�������Լ��������������ģ��Ϳ���ֱ��ʹ�õ�ǰ�����õ��ڴ��
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

