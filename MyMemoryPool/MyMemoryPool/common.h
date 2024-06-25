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


	const size_t MAX_BYTES = 64 * 1024;		// ThreadCache 申请的最大内存
	const size_t NLISTS = 184;				// 数组元素总的有多少个，由对齐规则计算得来
	const size_t PAGE_SHIFT = 12;			// Page大小 2^12 bytes.
	const size_t NPAGES = 129;				// Span 最多包含页数.

	// 从操作系统直接申请虚拟内存的函数
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

	// 读取当前内存块的前四个或者八个字节（取决于操作系统位数），解引用后得到下一个内存块的指针。
	inline ObjPtr& NextObj(ObjPtr& obj) {
		return *static_cast<ObjPtr*> (obj);
	}

	// 将内存块指针固定移动offset个字节。
	inline ObjPtr MovePtr(ObjPtr ptr, size_t offset) {
		return static_cast<void*>(static_cast<char*>(ptr) + offset);
	}

	// 内存对齐
	class Alignment {
	public:	
		inline static size_t _Index(size_t size, size_t align){
			// 计算给定大小的内存块在对齐后的索引位置。
			// align: 对齐的幂指数，表示要对齐到 2 ^ align (即 1 << align)字节。				
			size_t alignnum = 1 << align;  
			return ((size + alignnum - 1) >> align) - 1;
		}

		inline static size_t _Roundup(size_t size, size_t align){
			// 将给定的内存块大小对齐到最近的更大或相等的对齐边界。
			size_t alignnum = 1 << align;
			return (size + alignnum - 1) & ~(alignnum - 1);
		}
	public:
		// 控制在12%左右的内碎片浪费
		// [1,128]				8byte对齐 freelist[0,16)			align = 3
		// [129,1024]			16byte对齐 freelist[16,72)		align = 4
		// [1025,8*1024]		128byte对齐 freelist[72,128)		align = 7
		// [8*1024+1,64*1024]	1024byte对齐 freelist[128,184)	align = 10

		/*
		obj的freelists: 8, 16, 24, ..., 64*1024.
		
		
		
		*/
		inline static size_t Index(size_t size) {
			// 
			assert(size <= MAX_BYTES);

			// 每个区间有多少个链
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

		// ThreadCache从CentralCache一次性申请多个内存块obj时，内存块的申请数量。
		// 内存块的size越小，申请的数量越多，上限512个，下限2个。
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

		// 根据内存块obj的size计算中心缓存要从页缓存获取多大的span对象
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
		ObjPtr head_ = nullptr;		// 头指针
		size_t size_ = 0;			// 链表当前长度
		size_t maxsize_ = 1;		// 链表总容量
	public:

		// 入栈：插入单个obj到链表开头
		void Push(ObjPtr obj){
			NextObj(obj) = head_;
			head_ = obj;
			++size_;
		}

		// 入栈：插入多个obj组成的链条到链表开头
		void PushRange(ObjPtr start, ObjPtr end, size_t n)
		{
			NextObj(end) = head_;
			head_ = start;
			size_ += n;
		}

		// 出栈：弹出链表开头的单个obj.
		ObjPtr Pop() {
			ObjPtr obj = head_;
			head_ = NextObj(obj);
			--size_;

			return obj;
		}

		// 出栈：弹出所有obj组成的链条.
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

		// 设置最大容量。
		void SetMaxSize(size_t maxsize) {
			maxsize_ = maxsize;
		}

	};

	class Span {
	public:
		ObjPtr list_ = nullptr;	// 当前第一个未被分配的对象Obj的指针
		ObjSize objsize_ = 0;	// 划分的对象Obj大小
		size_t usecount_ = 0;	// 当前有多少对象Obj已经被分配

		PageId pageid_ = 0;		// 起始页号
		size_t npage_ = 0;		// 总页数

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

		// 删除复制构造函数和赋值操作符
		SpanList(const SpanList&) = delete;
		SpanList& operator=(const SpanList&) = delete;

		// 左闭右开
		SpanPtr Begin() {
			return head_->next_;
		}

		SpanPtr End() {
			return head_;
		}

		bool Empty() {
			return head_->next_ == head_;
		}

		//在pos位置的前面插入一个span
		void Insert(SpanPtr cur, SpanPtr newspan)
		{
			SpanPtr prev = cur->prev_;

			prev->next_ = newspan;
			newspan->next_ = cur;

			newspan->prev_ = prev;
			cur->prev_ = newspan;
		}

		//删除pos位置的span
		void Erase(SpanPtr cur)//此处只是单纯的把pos拿出来，并没有释放掉，后面还有用处
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


		// 获取一个有空闲obj的span
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

