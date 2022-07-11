#include "ngx_pool.h"

//内存池创建接口，这里选择让构造函数来调用，而不是外界显式地调用
void* NgxMemoryPool::ngx_create_pool(size_t size)
{
	pool_ = static_cast<ngx_pool_s*>(malloc(size));
	if (pool_ == nullptr)
	{
		return nullptr;
	}

	pool_->d.last = (u_char*)pool_ + sizeof(ngx_pool_s);
	pool_->d.end = (u_char*)pool_ + size;
	pool_->d.next = NULL;
	pool_->d.failed = 0;

	size = size - sizeof(ngx_pool_s);
	pool_->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

	pool_->current = pool_;

	pool_->large = nullptr;
	pool_->cleanup = nullptr;

	return pool_;

}

//构造函数
NgxMemoryPool::NgxMemoryPool(size_t size)
{
	this->ngx_create_pool(size);
}

//析构函数
NgxMemoryPool::~NgxMemoryPool()
{
	this->ngx_destroy_pool();
}

bool NgxMemoryPool::ngx_create_success()
{
	return nullptr != this->pool_;
}

//外界向内存池申请内存的接口，考虑内存对齐
void* NgxMemoryPool::ngx_palloc(size_t size)
{
	if (size <= pool_->max) {
		return ngx_palloc_small(size, 1);
	}
	return ngx_palloc_large(size);
}

//外界向内存池申请内存的接口，不考虑内存对齐
void* NgxMemoryPool::ngx_pnalloc(size_t size)
{
	if (size <= pool_->max) {
		return ngx_palloc_small(size, 0);
	}
	return ngx_palloc_large(size);
}

//外界向内存池申请内存的接口，考虑内存对齐，且内存初始值为0
void* NgxMemoryPool::ngx_pcalloc(size_t size)
{
	void* p = ngx_palloc(size);
	if (p)
	{
		memset(p, 0, size);
	}
	return p;
}

//对大块内存的释放
void NgxMemoryPool::ngx_pfree(void* p)
{
	ngx_pool_large_s* l;

	for (l = pool_->large; l; l = l->next) {
		if (p == l->alloc) {

			free(l->alloc);
			l->alloc = nullptr;
			return;
		}
	}
}

void NgxMemoryPool::ngx_reset_pool()
{
	ngx_pool_s* p;
	ngx_pool_large_s* l;

	for (l = pool_->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
		}
	}
	//先处理第一大块
	p = pool_;
	p->d.last = (u_char*)p + sizeof(ngx_pool_s);
	p->d.failed = 0;
	//再处理后续的块内存池
	for (p = p->d.next; p; p = p->d.next)
	{
		p->d.last = (u_char*)p + sizeof(ngx_pool_data_t);
		p->d.failed = 0;
	}

	pool_->current = pool_;
	pool_->large = NULL;
}

//内存池销毁接口，这里选择让析构函数来调用，所以设为私有
void NgxMemoryPool::ngx_destroy_pool()
{
	ngx_pool_s* p, * n;
	ngx_pool_large_s* l;
	ngx_pool_cleanup_s* c;

	//先处理好大块内存中维护的资源，调用它们的处理函数
	for (c = pool_->cleanup; c; c = c->next) {
		if (c->handler) {
			c->handler(c->data);
		}
	}
	//释放大块内存
	for (l = pool_->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
		}
	}
	//最后释放小块内存，因为大块内存等头信息保存在小块内存池中
	for (p = pool_, n = pool_->d.next; /* void */; p = n, n = n->d.next) {
		free(p);
		if (n == nullptr) {
			break;
		}
	}

}

//添加资源清除的回调函数，由于ngx_pool_cleanup_s* c的内存是由ngx_palloc分配而来的，而它显然较小，所以ngx_palloc会交付小块内存
ngx_pool_cleanup_s* NgxMemoryPool::ngx_pool_cleanup_add(size_t size)
{
	ngx_pool_cleanup_s* c;

	c = static_cast<ngx_pool_cleanup_s*>(ngx_palloc(sizeof(ngx_pool_cleanup_s)));
	if (c == nullptr) {
		return nullptr;
	}
	if (size) {
		c->data = ngx_palloc(size);
		if (c->data == nullptr) {
			return nullptr;
		}

	}
	else {
		c->data = nullptr;
	}

	c->handler = nullptr;
	c->next = pool_->cleanup;

	pool_->cleanup = c;

	return c;

}

//小块内存的获取
void* NgxMemoryPool::ngx_palloc_small(size_t size, ngx_uint_t align)
{
	u_char* m;
	ngx_pool_s* p;

	p = pool_->current;

	do {
		m = p->d.last;

		if (align) {
			m = ngx_align_ptr(m, NGX_ALIGNMENT);
		}

		if ((size_t)(p->d.end - m) >= size) {
			p->d.last = m + size;

			return m;
		}

		p = p->d.next;

	} while (p);

	return ngx_palloc_block(size);

}

//内存块的获取
void* NgxMemoryPool::ngx_palloc_block(size_t size)
{
	u_char* m;
	size_t       psize;
	ngx_pool_s* p, * newpool;

	psize = (size_t)(pool_->d.end - (u_char*)pool_);

	m = static_cast<u_char*>(malloc(psize));
	if (m == nullptr) {
		return nullptr;
	}

	newpool = (ngx_pool_s*)m;

	newpool->d.end = m + psize;
	newpool->d.next = nullptr;
	newpool->d.failed = 0;

	m += sizeof(ngx_pool_data_t);
	m = ngx_align_ptr(m, NGX_ALIGNMENT);
	newpool->d.last = m + size;

	for (p = pool_->current; p->d.next; p = p->d.next) {
		if (p->d.failed++ > 4) {
			pool_->current = p->d.next;
		}
	}

	p->d.next = newpool;

	return m;

}


//大块内存的获取
void* NgxMemoryPool::ngx_palloc_large(size_t size)
{
	void* p;
	ngx_uint_t         n;
	ngx_pool_large_s* large;

	p = malloc(size);
	if (p == nullptr) {
		return nullptr;
	}
	n = 0;
	for (large = pool_->large; large; large = large->next) {
		if (large->alloc == nullptr) {
			large->alloc = p;
			return p;
		}
		//找前三块还没找到alloc为空的数据头，就不找了，直接创建新的数据头
		if (n++ > 3) {
			break;
		}
	}

	large = static_cast<ngx_pool_large_s*>(ngx_palloc_small(sizeof(ngx_pool_large_s), 1));
	if (large == nullptr) {
		free(p);
		return nullptr;
	}

	large->alloc = p;
	large->next = pool_->large;
	pool_->large = large;

	return p;
}