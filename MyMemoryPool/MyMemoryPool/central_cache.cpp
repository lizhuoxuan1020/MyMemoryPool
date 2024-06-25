#include "central_cache.h"

namespace memory_pool {
	CentralCache CentralCache::inst_;

	SpanPtr CentralCache::GetOneSpan(SpanList& spanlist, size_t byte_size) {
		SpanPtr span = spanlist.Begin();
		while (span != spanlist.End())//��ǰ�ҵ�һ��span
		{
			if (span->list_ != nullptr)
				return span;
			else
				span = span->next_;
		}

		// �ߵ������˵��ǰ��û�л�ȡ��span,���ǿյģ�����һ��PageHeap��ȡspan
		SpanPtr newspan = PageHeap::GetInstance()->AllocateSpan(Alignment::SpanPagesForObj(byte_size));
		// ��spanҳ�зֳ���Ҫ�Ķ�����������
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


	//��ȡһ���������ڴ����
	size_t CentralCache::AllocateObjList(ObjPtr& start, ObjPtr& end, size_t n, ObjSize byte_size) {
		size_t index = Alignment::Index(byte_size);
		SpanList& spanlist = spanlists_[index];//��ֵ->��������

		//����
		std::unique_lock<std::mutex> lock(spanlist.mutex_);

		SpanPtr span = GetOneSpan(spanlist, byte_size);
		//������Ѿ���ȡ��һ��newspan

		//��span�л�ȡrange����
		size_t batchsize = 0;
		void* prev = nullptr;//��ǰ����ǰһ��
		void* cur = span->list_;//��cur��������������
		for (size_t i = 0; i < n; ++i)
		{
			prev = cur;
			cur = NextObj(cur);
			++batchsize;
			if (cur == nullptr)//��ʱ�ж�cur�Ƿ�Ϊ�գ�Ϊ�յĻ�����ǰֹͣ
				break;
		}

		start = span->list_;
		end = prev;

		span->list_ = cur;
		span->usecount_ += batchsize;

		//���յ�span�Ƶ���󣬱��ַǿյ�span��ǰ��
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

		//��������ѭ������
		// CentralCache:�Ե�ǰͰ���м���(Ͱ��)����С��������
		// PageHeap:���������SpanListȫ�ּ���
		// ��Ϊ���ܴ��ڶ���߳�ͬʱȥϵͳ�����ڴ�����

		std::unique_lock<std::mutex> lock(spanlist.mutex_);

		while (start != nullptr)
		{
			ObjPtr next = NextObj(start);

			////��ʱ��ǵü���
			//spanlist.Lock(); // �����˺ܶ��������

			SpanPtr span = PageHeap::GetInstance()->FindSpan(start);
			NextObj(start) = span->list_;
			span->list_ = start;
			//��һ��span�Ķ���ȫ���ͷŻ�����ʱ�򣬽�span����PageHeap,������ҳ�ϲ�
			if (--span->usecount_ == 0){
				spanlist.Erase(span);
				PageHeap::GetInstance()->ReclaimSpan(span);
			}

			start = next;
		}
	}
} // memory_pool
