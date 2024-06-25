#include "thread_cache.h"

namespace memory_pool {

	// ����һ������Ļ�����һ����������������������
	ObjPtr ThreadCache::FetchFromCentralCache(size_t index, ObjSize size){
		FreeList* freelist = &freelists_[index];

		// ��ʼ��Ϊ1����
		// ��������ԽС�������ڴ�������Խ�ࣺ �� Alignment::ObjNumToApply(size) ���ơ�
		// ĳ��size������������Խ�࣬��������Խ�ࣺ �� freelist->maxsize ���ơ�
		// ���ֿ�������ȡ��Сֵ��
		size_t maxsize = freelist->MaxSize();
		size_t num_to_apply = min(Alignment::ApplyNumForObj(size), maxsize);

		// �� start��end ����ָ���ʶ ����ɹ��������ڴ������Ŀ�ʼ��ַ�ͽ�����ַ
		void* start = nullptr, * end = nullptr;
		
		// batchsize ��ʶ����ɹ��������ڴ�����������С��num����ʾ���Ļ���û����ô���С���ڴ�顣
		size_t batchsize = CentralCache::GetInstance()->AllocateObjList(start, end, num_to_apply, size);

		// ����ɹ���push��freelist.
		if (batchsize > 1) {
			freelist->PushRange(NextObj(start), end, batchsize - 1);
		}

		// ���������maxsize�ĸ�����maxsize��1.
		if (batchsize >= freelist->MaxSize()) {
			freelist->SetMaxSize(maxsize + 1);
		}

		return start;
	}

	// ����������ڴ泤��size,������Ҫ������ڴ��obj����������û���
	ObjPtr ThreadCache::AllocateObj(size_t size){

		// �������󳤶�size��Ӧ��freelistͰ��
		size_t index = Alignment::Index(size);
		FreeList* freelist = &freelists_[index];

		// ���Ͱ��Ϊ�գ�ֱ�ӷ������еĵ�һ���ڴ��obj.
		if (!freelist->Empty()) {
			return freelist->Pop();
		}

		// ͰΪ��ʱ������һ��CentralCacheһ�����������ȳߴ���ڴ�顣
		// ���������ļ���������������ԣ�����ȡ�Ĵ������Ӷ��ڴ����������ӡ�
		else{
			return FetchFromCentralCache(index, Alignment::RoundUp(size));
		}
	}

	// �����ڴ��obj��freelists_.
	void ThreadCache::ReclaimObj(ObjPtr obj, ObjSize size){
		size_t index = Alignment::Index(size);
		FreeList* freelist = &freelists_[index];
		freelist->Push(obj);

		// ��Ӧ��freelist̫��ʱ������Ͱ�������ڴ�鶼���ظ�CentralCache.
		if (freelist->Size() >= freelist->MaxSize()){
			void* start = freelist->PopRange();
			CentralCache::GetInstance()->ReclaimObjList(start, size);
		}
	}
} // namespace memory_pool
