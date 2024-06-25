#include "thread_cache.h"

namespace memory_pool {

	// 从上一层的中心缓存中一次性申请多个对象（慢启动）
	ObjPtr ThreadCache::FetchFromCentralCache(size_t index, ObjSize size){
		FreeList* freelist = &freelists_[index];

		// 初始都为1个。
		// 单个对象越小，申请内存块的数量越多： 由 Alignment::ObjNumToApply(size) 控制。
		// 某个size对象的申请次数越多，申请数量越多： 由 freelist->maxsize 控制。
		// 两种控制因素取较小值。
		size_t maxsize = freelist->MaxSize();
		size_t num_to_apply = min(Alignment::ApplyNumForObj(size), maxsize);

		// 用 start，end 两个指针标识 申请成功的连续内存块链表的开始地址和结束地址
		void* start = nullptr, * end = nullptr;
		
		// batchsize 标识申请成功的连续内存块个数，可能小于num，表示中心缓存没有那么多大小的内存块。
		size_t batchsize = CentralCache::GetInstance()->AllocateObjList(start, end, num_to_apply, size);

		// 申请成功，push入freelist.
		if (batchsize > 1) {
			freelist->PushRange(NextObj(start), end, batchsize - 1);
		}

		// 如果申请了maxsize的个数，maxsize加1.
		if (batchsize >= freelist->MaxSize()) {
			freelist->SetMaxSize(maxsize + 1);
		}

		return start;
	}

	// 根据请求的内存长度size,计算需要分配的内存块obj，并分配给用户。
	ObjPtr ThreadCache::AllocateObj(size_t size){

		// 计算请求长度size对应的freelist桶。
		size_t index = Alignment::Index(size);
		FreeList* freelist = &freelists_[index];

		// 如果桶不为空，直接分配其中的第一个内存块obj.
		if (!freelist->Empty()) {
			return freelist->Pop();
		}

		// 桶为空时，到上一层CentralCache一次性申请多个等尺寸的内存块。
		// 申请数量的计算采用慢启动策略：随着取的次数增加而内存对象个数增加。
		else{
			return FetchFromCentralCache(index, Alignment::RoundUp(size));
		}
	}

	// 回收内存块obj到freelists_.
	void ThreadCache::ReclaimObj(ObjPtr obj, ObjSize size){
		size_t index = Alignment::Index(size);
		FreeList* freelist = &freelists_[index];
		freelist->Push(obj);

		// 对应的freelist太长时，将该桶的所有内存块都返回给CentralCache.
		if (freelist->Size() >= freelist->MaxSize()){
			void* start = freelist->PopRange();
			CentralCache::GetInstance()->ReclaimObjList(start, size);
		}
	}
} // namespace memory_pool
