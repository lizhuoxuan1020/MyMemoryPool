#pragma once

#include "common.h"
#include "page_heap.h"

namespace memory_pool {
	//单例模式
	class CentralCache
	{
	public:
		static CentralCache* GetInstance() {
			return &inst_;
		}

		// 从中心缓存获取一定数量的内存块obj给thread cache.
		// 用 start，end 两个指针标识 申请成功的连续内存块链表的开始地址和结束地址
		size_t AllocateObjList(ObjPtr& start, ObjPtr& end, size_t n, ObjSize byte_size);
		//将一定数量的对象回收给span.
		void ReclaimObjList(ObjPtr start, size_t size);

	private:
		SpanList spanlists_[NLISTS];

		SpanPtr GetOneSpan(SpanList& spanlist, size_t byte_size);

	private:
		CentralCache() {}
		CentralCache(const CentralCache&) = delete;
		CentralCache& operator=(const CentralCache&) = delete;

		static CentralCache inst_;
	};

} // namespace memory_pool
