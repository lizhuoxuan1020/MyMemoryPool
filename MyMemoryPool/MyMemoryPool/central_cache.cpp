#include "central_cache.h"

namespace memory_pool {
	CentralCache CentralCache::inst_;

	SpanPtr CentralCache::GetOneSpan(SpanList& spanlist, size_t byte_size) {
		SpanPtr span = spanlist.Begin();
		while (span != spanlist.End())//当前找到一个span
		{
			if (span->list_ != nullptr)
				return span;
			else
				span = span->next_;
		}

		// 走到这儿，说明前面没有获取到span,都是空的，到下一层PageHeap获取span
		SpanPtr newspan = PageHeap::GetInstance()->AllocateSpan(Alignment::SpanPagesForObj(byte_size));
		// 将span页切分成需要的对象并链接起来
		ObjPtr cur = reinterpret_cast<ObjPtr>(newspan->pageid_ << PAGE_SHIFT);
		ObjPtr end = MovePtr(cur, newspan->npage_ << PAGE_SHIFT);
		newspan->list_ = cur;
		newspan->objsize_ = byte_size;

		while (MovePtr(cur, byte_size) < end){
			ObjPtr next = MovePtr(cur, byte_size);
			NextObj(cur) = next;
			cur = next;
		}
		NextObj(cur) = nullptr;

		spanlist.PushFront(newspan);

		return newspan;
	}


	//获取一个批量的内存对象
	size_t CentralCache::AllocateObjList(ObjPtr& start, ObjPtr& end, size_t n, ObjSize byte_size) {
		size_t index = Alignment::Index(byte_size);
		SpanList& spanlist = spanlists_[index];//赋值->拷贝构造

		//加锁
		std::unique_lock<std::mutex> lock(spanlist.mutex_);

		SpanPtr span = GetOneSpan(spanlist, byte_size);
		//到这儿已经获取到一个newspan

		//从span中获取range对象
		size_t batchsize = 0;
		void* prev = nullptr;//提前保存前一个
		void* cur = span->list_;//用cur来遍历，往后走
		for (size_t i = 0; i < n; ++i)
		{
			prev = cur;
			cur = NextObj(cur);
			++batchsize;
			if (cur == nullptr)//随时判断cur是否为空，为空的话，提前停止
				break;
		}

		start = span->list_;
		end = prev;

		span->list_ = cur;
		span->usecount_ += batchsize;

		//将空的span移到最后，保持非空的span在前面
		if (span->list_ == nullptr)
		{
			spanlist.Erase(span);
			spanlist.PushBack(span);
		}


		return batchsize;
	}

	void CentralCache::ReclaimObjList(ObjPtr start, size_t size) {
		size_t index = Alignment::Index(size);
		SpanList& spanlist = spanlists_[index];

		//将锁放在循环外面
		// CentralCache:对当前桶进行加锁(桶锁)，减小锁的粒度
		// PageHeap:必须对整个SpanList全局加锁
		// 因为可能存在多个线程同时去系统申请内存的情况

		std::unique_lock<std::mutex> lock(spanlist.mutex_);

		while (start != nullptr)
		{
			ObjPtr next = NextObj(start);

			////到时候记得加锁
			//spanlist.Lock(); // 构成了很多的锁竞争

			SpanPtr span = PageHeap::GetInstance()->FindSpan(start);
			NextObj(start) = span->list_;
			span->list_ = start;
			//当一个span的对象全部释放回来的时候，将span还给PageHeap,并且做页合并
			if (--span->usecount_ == 0){
				spanlist.Erase(span);
				PageHeap::GetInstance()->ReclaimSpan(span);
			}

			start = next;
		}
	}
} // memory_pool
