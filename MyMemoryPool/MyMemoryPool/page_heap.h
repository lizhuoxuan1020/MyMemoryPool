#pragma once
#include "common.h"

namespace memory_pool {

	// µ¥ÀýÄ£Ê½
	class PageHeap {
	public:
		static PageHeap* GetInstance() {
			return &inst_;
		}

	public:
		SpanPtr FindSpan(ObjPtr obj);

		SpanPtr AllocateLargeSpan(size_t size);

		void ReclaimLargeSpan(SpanPtr span);

		SpanPtr AllocateSpan(size_t size);
		void ReclaimSpan(SpanPtr span);

	private:
		SpanList spanlists_[NPAGES];
		std::unordered_map<PageId, SpanPtr> page_span_map_;
		std::mutex mutex_;

	private:
		PageHeap() {}
		PageHeap(PageHeap&) = delete;
		PageHeap& operator=(const PageHeap&) = delete;

		static PageHeap inst_;
	};

} // namespace memory_pool


