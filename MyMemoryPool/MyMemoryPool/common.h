#pragma once

#include <mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>

#ifdef _WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif // _WIN32

namespace memory_pool {
	class Span;

	using PageId = size_t;
	using ObjPtr = void*;
	using ObjSize = size_t;
	using SpanPtr = Span*;
	using SpanSize = size_t;


	const size_t MAX_BYTES = 64 * 1024;		// ThreadCache ���������ڴ�
	const size_t NLISTS = 184;				// ����Ԫ���ܵ��ж��ٸ����ɶ������������
	const size_t PAGE_SHIFT = 12;			// Page��С 2^12 bytes.
	const size_t NPAGES = 129;				// Span ������ҳ��.

	// �Ӳ���ϵͳֱ�����������ڴ�ĺ���
#ifdef _WIN32
	inline ObjPtr SysAlloc(ObjSize size) {
		ObjPtr ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		return ptr;
	}
	inline void SysFree(ObjPtr ptr, ObjSize size) {
		VirtualFree(ptr, 0, MEM_RELEASE);
	}

#elif defined(__linux__)
	inline ObjPtr SysAlloc(ObjSize size) {
		ObjPtr mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, 
						  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		return ptr;
	}
	inline void SysFree(ObjPtr ptr, ObjSize size) {
		munmap(mem, size);
	}
#endif //_WIN32

	// ��ȡ��ǰ�ڴ���ǰ�ĸ����߰˸��ֽڣ�ȡ���ڲ���ϵͳλ�����������ú�õ���һ���ڴ���ָ�롣
	inline ObjPtr& NextObj(ObjPtr& obj) {
		return *static_cast<ObjPtr*> (obj);
	}

	// ���ڴ��ָ��̶��ƶ�offset���ֽڡ�
	inline ObjPtr MovePtr(ObjPtr ptr, size_t offset) {
		return static_cast<void*>(static_cast<char*>(ptr) + offset);
	}

	// �ڴ����
	class Alignment {
	public:	
		inline static size_t _Index(size_t size, size_t align){
			// ���������С���ڴ���ڶ���������λ�á�
			// align: �������ָ������ʾҪ���뵽 2 ^ align (�� 1 << align)�ֽڡ�				
			size_t alignnum = 1 << align;  
			return ((size + alignnum - 1) >> align) - 1;
		}

		inline static size_t _Roundup(size_t size, size_t align){
			// ���������ڴ���С���뵽����ĸ������ȵĶ���߽硣
			size_t alignnum = 1 << align;
			return (size + alignnum - 1) & ~(alignnum - 1);
		}
	public:
		// ������12%���ҵ�����Ƭ�˷�
		// [1,128]				8byte���� freelist[0,16)			align = 3
		// [129,1024]			16byte���� freelist[16,72)		align = 4
		// [1025,8*1024]		128byte���� freelist[72,128)		align = 7
		// [8*1024+1,64*1024]	1024byte���� freelist[128,184)	align = 10

		/*
		obj��freelists: 8, 16, 24, ..., 64*1024.
		
		
		
		*/
		inline static size_t Index(size_t size) {
			// 
			assert(size <= MAX_BYTES);

			// ÿ�������ж��ٸ���
			static int group_array[4] = { 16, 56, 56, 56 };
			if (size <= 128)
			{
				return _Index(size, 3);
			}
			else if (size <= 1024)
			{
				return _Index(size - 128, 4) + group_array[0];
			}
			else if (size <= 8192)
			{
				return _Index(size - 1024, 7) + group_array[0] + group_array[1];
			}
			else//if (size <= 65536)
			{
				return _Index(size - 8 * 1024, 10) + group_array[0] + 
					          group_array[1] + group_array[2];
			}
		}

		inline static size_t RoundUp(size_t bytes) {
			// 
			assert(bytes <= MAX_BYTES);

			if (bytes <= 128) {
				return _Roundup(bytes, 3);
			}
			else if (bytes <= 1024) {
				return _Roundup(bytes, 4);
			}
			else if (bytes <= 8192) {
				return _Roundup(bytes, 7);
			}
			else {//if (bytes <= 65536){
				return _Roundup(bytes, 10);
			}
		}

		// ThreadCache��CentralCacheһ�����������ڴ��objʱ���ڴ�������������
		// �ڴ���sizeԽС�����������Խ�࣬����512��������2����
		inline static size_t ApplyNumForObj(ObjSize size){
			if (size == 0)
				return 0;
			
			int num = static_cast<int>(MAX_BYTES / size);
			if (num < 2)
				num = 2;

			if (num > 512)
				num = 512;

			return num;
		}

		// �����ڴ��obj��size�������Ļ���Ҫ��ҳ�����ȡ����span����
		inline static size_t SpanPagesForObj(ObjSize size){
			size_t num = ApplyNumForObj(size);
			size_t npage = num * size;
			npage >>= PAGE_SHIFT;
			if (npage == 0)
				npage = 1;

			return npage;
		}
	};


	class FreeList {
	private:
		ObjPtr head_ = nullptr;		// ͷָ��
		size_t size_ = 0;			// ����ǰ����
		size_t maxsize_ = 1;		// ����������
	public:

		// ��ջ�����뵥��obj������ͷ
		void Push(ObjPtr obj){
			NextObj(obj) = head_;
			head_ = obj;
			++size_;
		}

		// ��ջ��������obj��ɵ�����������ͷ
		void PushRange(ObjPtr start, ObjPtr end, size_t n)
		{
			NextObj(end) = head_;
			head_ = start;
			size_ += n;
		}

		// ��ջ����������ͷ�ĵ���obj.
		ObjPtr Pop() {
			ObjPtr obj = head_;
			head_ = NextObj(obj);
			--size_;

			return obj;
		}

		// ��ջ����������obj��ɵ�����.
		ObjPtr PopRange() {
			size_ = 0;
			ObjPtr list = head_;
			head_ = nullptr;

			return list;
		}

		bool Empty() {
			return head_ == nullptr;
		}

		size_t Size() {
			return size_;
		}

		size_t MaxSize() {
			return maxsize_;
		}

		// �������������
		void SetMaxSize(size_t maxsize) {
			maxsize_ = maxsize;
		}

	};

	class Span {
	public:
		ObjPtr list_ = nullptr;	// ��ǰ��һ��δ������Ķ���Obj��ָ��
		ObjSize objsize_ = 0;	// ���ֵĶ���Obj��С
		size_t usecount_ = 0;	// ��ǰ�ж��ٶ���Obj�Ѿ�������

		PageId pageid_ = 0;		// ��ʼҳ��
		size_t npage_ = 0;		// ��ҳ��

		SpanPtr prev_ = nullptr;
		SpanPtr next_ = nullptr;
	};

	class SpanList
	{
	public:
		SpanPtr head_;
		std::mutex mutex_;

	public:
		SpanList() {
			head_ = new Span;
			head_->next_ = head_;
			head_->prev_ = head_;
		}

		~SpanList() {
			SpanPtr cur = head_->next_;
			while (cur != head_) {
				SpanPtr next = cur->next_;
				delete cur;
				cur = next;
			}
			delete head_;
			head_ = nullptr;
		}

		// ɾ�����ƹ��캯���͸�ֵ������
		SpanList(const SpanList&) = delete;
		SpanList& operator=(const SpanList&) = delete;

		// ����ҿ�
		SpanPtr Begin() {
			return head_->next_;
		}

		SpanPtr End() {
			return head_;
		}

		bool Empty() {
			return head_->next_ == head_;
		}

		//��posλ�õ�ǰ�����һ��span
		void Insert(SpanPtr cur, SpanPtr newspan)
		{
			SpanPtr prev = cur->prev_;

			prev->next_ = newspan;
			newspan->next_ = cur;

			newspan->prev_ = prev;
			cur->prev_ = newspan;
		}

		//ɾ��posλ�õ�span
		void Erase(SpanPtr cur)//�˴�ֻ�ǵ����İ�pos�ó�������û���ͷŵ������滹���ô�
		{
			SpanPtr prev = cur->prev_;
			SpanPtr next = cur->next_;

			prev->next_ = next;
			next->prev_ = prev;
		}

		void PushBack(SpanPtr newspan) {
			Insert(End(), newspan);
		}

		void PushFront(SpanPtr newspan) {
			Insert(Begin(), newspan);
		}

		SpanPtr PopBack() {
			SpanPtr span = head_->prev_;
			Erase(span);
			return span;
		}

		SpanPtr PopFront() {
			SpanPtr span = head_->next_;
			Erase(span);
			return span;
		}


		// ��ȡһ���п���obj��span
		SpanPtr GetOneSpan() {
			Span* span = Begin();
			while (span != End()) {
				if (span->list_ != nullptr) {
					return span;
				}
				span = span->next_;
			}
			return nullptr;
		}
	};
} // namespace memory_pool

