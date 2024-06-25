#pragma once

#include "common.h"
#include "central_cache.h"

namespace memory_pool {
	class ThreadCache {
	private:
		FreeList freelists_[NLISTS];//自由链表

	public:
		// 分配和回收内存对象obj
		ObjPtr AllocateObj(size_t size);
		void ReclaimObj(ObjPtr obj, ObjSize size);

	private:
		// 从上一层的中心缓存中一次性申请多个对象（慢启动）
		void* FetchFromCentralCache(size_t index, size_t size);
	};

	//每个线程都有自己的tlslist, 不需要加锁。
	thread_local static ThreadCache* tlslist = nullptr;
} // namespace memory_pool
