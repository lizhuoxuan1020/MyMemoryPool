#pragma once

#include "common.h"
#include "central_cache.h"

namespace memory_pool {
	class ThreadCache {
	private:
		FreeList freelists_[NLISTS];//��������

	public:
		// ����ͻ����ڴ����obj
		ObjPtr AllocateObj(size_t size);
		void ReclaimObj(ObjPtr obj, ObjSize size);

	private:
		// ����һ������Ļ�����һ����������������������
		void* FetchFromCentralCache(size_t index, size_t size);
	};

	//ÿ���̶߳����Լ���tlslist, ����Ҫ������
	thread_local static ThreadCache* tlslist = nullptr;
} // namespace memory_pool
