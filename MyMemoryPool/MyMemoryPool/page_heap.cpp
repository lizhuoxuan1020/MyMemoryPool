#include "page_heap.h"

namespace memory_pool {
	PageHeap PageHeap::inst_;

	// �ɶ���obj��ѯ������span
	SpanPtr PageHeap::FindSpan(ObjPtr obj){
		PageId id = reinterpret_cast<PageId>(obj) >> PAGE_SHIFT;
		auto it = page_span_map_.find(id);
		if (it != page_span_map_.end()){
			return it->second;
		}
		return nullptr;
	}

	// ���䳬��ThreadCache::freelists_���Ͱ���ڴ��ߴ�MAX_BYTES�Ĵ��ڴ�顣
	// ��������£��û��ӿڻ�ֱ�ӵ��ñ��������������м��CentralCache.
	SpanPtr PageHeap::AllocateLargeSpan(size_t size) {
		assert(size > MAX_BYTES);

		size = Alignment::_Roundup(size, PAGE_SHIFT); 
		size_t npage = size >> PAGE_SHIFT;

		// ҳ��������ҳ��spanlists���ҳ��
		if (npage < NPAGES){
			SpanPtr span = AllocateSpan(npage);
			span->objsize_ = size;
			return span;
		}
		// ����Ӳ���ϵͳֱ�����룬�������ϣmap.
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

	// ���ճ���ThreadCache::freelists_���Ͱ���ڴ��ߴ�MAX_BYTES�Ĵ��ڴ�顣
	// ��������£�ThreadCache��ֱ�ӵ��ñ��������������м��CentralCache.
	void PageHeap::ReclaimLargeSpan(SpanPtr span) {
		size_t npage = span->objsize_ >> PAGE_SHIFT;
		void* ptr = reinterpret_cast<void*>((span->pageid_ << PAGE_SHIFT));

		// ҳ��������ҳ��spanlists���ҳ��
		if (npage < NPAGES)
		{
			span->objsize_ = 0;
			ReclaimSpan(span);
		}
		// ���������ϵͳֱ�ӷ��������ӹ�ϣmap��ɾ��.
		else{
			page_span_map_.erase(npage);			
			SysFree(ptr, span->objsize_);
			delete span;
		}
	}


	// ����span
	SpanPtr PageHeap::AllocateSpan(size_t n) {	
		// ��������ֹ����߳�ͬʱ��PageHeap������span
		// ��������Ǹ�ȫ�ּ��������ܵ����ĸ�ÿ��Ͱ����
		// �����ӦͰû��span,����Ҫ��ϵͳ�����
		// ���ܴ��ڶ���߳�ͬʱ��ϵͳ�����ڴ�Ŀ���
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

		// ������˵��SpanList��û�к��ʵ�span,ֻ����ϵͳ����128ҳ���ڴ�
		ObjPtr ptr = SysAlloc((NPAGES - 1) * (1 << PAGE_SHIFT));


		span->pageid_ = (PageId)ptr >> PAGE_SHIFT;
		span->npage_ = NPAGES - 1;

		for (size_t i = 0; i < span->npage_; ++i)
			page_span_map_[span->pageid_ + i] = span;

		spanlists_[span->npage_].PushFront(span);  //������
		return AllocateSpan(n);
	}


	// �ջ�span
	void PageHeap::ReclaimSpan(SpanPtr cur) {
		// ȫ����,���ܶ���߳�һ���ThreadCache�й黹���ݣ����漰��Ͱ�ĺϲ�������
		std::unique_lock<std::mutex> lock(mutex_);


		// ���ͷŵ��ڴ��Ǵ���128ҳ,ֱ�ӽ��ڴ�黹������ϵͳ,���ܺϲ�
		if (cur->npage_ >= NPAGES) {
			void* ptr = (void*)(cur->pageid_ << PAGE_SHIFT);
			// �黹֮ǰɾ����ҳ��span��ӳ��
			page_span_map_.erase(cur->pageid_);
			VirtualFree(ptr, 0, MEM_RELEASE);
			delete cur;
			return;
		}


		// ��ǰ�ϲ�
		while (1) {
			////����128ҳ�򲻺ϲ�
			//if (cur->npage_ > NPAGES - 1)
			//	break;

			PageId curid = cur->pageid_;
			PageId previd = curid - 1;
			auto it = page_span_map_.find(previd);

			// û���ҵ�
			if (it == page_span_map_.end())
				break;

			// ǰһ��span������
			if (it->second->usecount_ != 0)
				break;

			SpanPtr prev = it->second;

			//����128ҳ�򲻺ϲ�
			if (cur->npage_ + prev->npage_ > NPAGES - 1)
				break;


			// �Ȱ�prev���������Ƴ�
			spanlists_[prev->npage_].Erase(prev);

			// �ϲ�
			prev->npage_ += cur->npage_;
			//����id->span��ӳ���ϵ
			for (PageId i = 0; i < cur->npage_; ++i) {
				page_span_map_[cur->pageid_ + i] = prev;
			}
			delete cur;

			// ������ǰ�ϲ�
			cur = prev;
		}


		//���ϲ�
		while (1) {
			////����128ҳ�򲻺ϲ�
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

			//����128ҳ�򲻺ϲ�
			if (cur->npage_ + next->npage_ >= NPAGES - 1)
				break;

			spanlists_[next->npage_].Erase(next);

			cur->npage_ += next->npage_;
			//����id->Span��ӳ���ϵ
			for (PageId i = 0; i < next->npage_; ++i) {
				page_span_map_[next->pageid_ + i] = cur;
			}

			delete next;
		}

		// ��󽫺ϲ��õ�span���뵽span����
		spanlists_[cur->npage_].PushFront(cur);
	}
} // namespace memory_pool

