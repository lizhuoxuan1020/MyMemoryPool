#pragma once

#include "common.h"
#include "page_heap.h"

namespace memory_pool {
	//����ģʽ
	class CentralCache
	{
	public:
		static CentralCache* GetInstance() {
			return &inst_;
		}

		// �����Ļ����ȡһ���������ڴ��obj��thread cache.
		// �� start��end ����ָ���ʶ ����ɹ��������ڴ������Ŀ�ʼ��ַ�ͽ�����ַ
		size_t AllocateObjList(ObjPtr& start, ObjPtr& end, size_t n, ObjSize byte_size);
		//��һ�������Ķ�����ո�span.
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
