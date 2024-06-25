#include "page_heap.h"

namespace memory_pool {
	PageHeap PageHeap::inst_;

	// 由对象obj查询所属的span
	SpanPtr PageHeap::FindSpan(ObjPtr obj){
		PageId id = reinterpret_cast<PageId>(obj) >> PAGE_SHIFT;
		auto it = page_span_map_.find(id);
		if (it != page_span_map_.end()){
			return it->second;
		}
		return nullptr;
	}

	// 分配超过ThreadCache::freelists_最大桶的内存块尺寸MAX_BYTES的大内存块。
	// 这种情况下，用户接口会直接调用本函数，不经过中间层CentralCache.
	SpanPtr PageHeap::AllocateLargeSpan(size_t size) {
		assert(size > MAX_BYTES);

		size = Alignment::_Roundup(size, PAGE_SHIFT); 
		size_t npage = size >> PAGE_SHIFT;

		// 页数不超过页堆spanlists最大页数
		if (npage < NPAGES){
			SpanPtr span = AllocateSpan(npage);
			span->objsize_ = size;
			return span;
		}
		// 否则从操作系统直接申请，并存入哈希map.
		else{
			ObjPtr ptr = SysAlloc(size);

			if (ptr == nullptr) {
				throw std::bad_alloc();
			}			

			SpanPtr span = new Span;
			span->npage_ = npage;
			span->pageid_ = (PageId)ptr >> PAGE_SHIFT;
			span->objsize_ = npage << PAGE_SHIFT;

			page_span_map_[span->pageid_] = span;

			return span;
		}
	}

	// 回收超过ThreadCache::freelists_最大桶的内存块尺寸MAX_BYTES的大内存块。
	// 这种情况下，ThreadCache会直接调用本函数，不经过中间层CentralCache.
	void PageHeap::ReclaimLargeSpan(SpanPtr span) {
		size_t npage = span->objsize_ >> PAGE_SHIFT;
		void* ptr = reinterpret_cast<void*>((span->pageid_ << PAGE_SHIFT));

		// 页数不超过页堆spanlists最大页数
		if (npage < NPAGES)
		{
			span->objsize_ = 0;
			ReclaimSpan(span);
		}
		// 否则向操作系统直接返还，并从哈希map中删除.
		else{
			page_span_map_.erase(npage);			
			SysFree(ptr, span->objsize_);
			delete span;
		}
	}


	// 分配span
	SpanPtr PageHeap::AllocateSpan(size_t n) {	
		// 加锁，防止多个线程同时到PageHeap中申请span
		// 这里必须是给全局加锁，不能单独的给每个桶加锁
		// 如果对应桶没有span,是需要向系统申请的
		// 可能存在多个线程同时向系统申请内存的可能
		std::unique_lock<std::mutex> lock(mutex_);

		assert(n < NPAGES);

		if (!spanlists_[n].Empty())
			return spanlists_[n].PopFront();

		for (size_t i = n + 1; i < NPAGES; ++i) {
			if (!spanlists_[i].Empty()) {
				SpanPtr span = spanlists_[i].PopFront();
				SpanPtr splist = new Span;

				splist->pageid_ = span->pageid_;
				splist->npage_ = n;
				span->pageid_ = span->pageid_ + n;
				span->npage_ = span->npage_ - n;

				//splist->pageid_ = span->pageid_ + n;
				//span->npage_ = splist->npage_ - n;
				//span->npage_ = n;

				for (size_t i = 0; i < n; ++i)
					page_span_map_[splist->pageid_ + i] = splist;

				//spanlist_[splist->npage_].PushFront(splist);
				//return span;

				spanlists_[span->npage_].PushFront(span);
				return splist;
			}
		}

		SpanPtr span = new Span;

		// 到这里说明SpanList中没有合适的span,只能向系统申请128页的内存
		ObjPtr ptr = SysAlloc((NPAGES - 1) * (1 << PAGE_SHIFT));


		span->pageid_ = (PageId)ptr >> PAGE_SHIFT;
		span->npage_ = NPAGES - 1;

		for (size_t i = 0; i < span->npage_; ++i)
			page_span_map_[span->pageid_ + i] = span;

		spanlists_[span->npage_].PushFront(span);  //方括号
		return AllocateSpan(n);
	}


	// 收回span
	void PageHeap::ReclaimSpan(SpanPtr cur) {
		// 全局锁,可能多个线程一起从ThreadCache中归还数据，且涉及跨桶的合并操作。
		std::unique_lock<std::mutex> lock(mutex_);


		// 当释放的内存是大于128页,直接将内存归还给操作系统,不能合并
		if (cur->npage_ >= NPAGES) {
			void* ptr = (void*)(cur->pageid_ << PAGE_SHIFT);
			// 归还之前删除掉页到span的映射
			page_span_map_.erase(cur->pageid_);
			VirtualFree(ptr, 0, MEM_RELEASE);
			delete cur;
			return;
		}


		// 向前合并
		while (1) {
			////超过128页则不合并
			//if (cur->npage_ > NPAGES - 1)
			//	break;

			PageId curid = cur->pageid_;
			PageId previd = curid - 1;
			auto it = page_span_map_.find(previd);

			// 没有找到
			if (it == page_span_map_.end())
				break;

			// 前一个span不空闲
			if (it->second->usecount_ != 0)
				break;

			SpanPtr prev = it->second;

			//超过128页则不合并
			if (cur->npage_ + prev->npage_ > NPAGES - 1)
				break;


			// 先把prev从链表中移除
			spanlists_[prev->npage_].Erase(prev);

			// 合并
			prev->npage_ += cur->npage_;
			//修正id->span的映射关系
			for (PageId i = 0; i < cur->npage_; ++i) {
				page_span_map_[cur->pageid_ + i] = prev;
			}
			delete cur;

			// 继续向前合并
			cur = prev;
		}


		//向后合并
		while (1) {
			////超过128页则不合并
			//if (cur->npage_ > NPAGES - 1)
			//	break;

			PageId curid = cur->pageid_;
			PageId nextid = curid + cur->npage_;
			//std::map<PageId, SpanPtr>::iterator it = page_span_map_.find(nextid);
			auto it = page_span_map_.find(nextid);

			if (it == page_span_map_.end())
				break;

			if (it->second->usecount_ != 0)
				break;

			SpanPtr next = it->second;

			//超过128页则不合并
			if (cur->npage_ + next->npage_ >= NPAGES - 1)
				break;

			spanlists_[next->npage_].Erase(next);

			cur->npage_ += next->npage_;
			//修正id->Span的映射关系
			for (PageId i = 0; i < next->npage_; ++i) {
				page_span_map_[next->pageid_ + i] = cur;
			}

			delete next;
		}

		// 最后将合并好的span插入到span链中
		spanlists_[cur->npage_].PushFront(cur);
	}
} // namespace memory_pool

