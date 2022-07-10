/**************************************************************************
 * File name: ngx_pool.h
 * Description: Use C++ to reconstruct the memory pool module of Nginx based on OOP.
 * Version: 1.0
 * Author: Fuming Liu
 * Date: July 10, 2022
 **************************************************************************/


#ifndef __NGXPOOL_H__
#define __NGXPOOL_H__

#include <stdlib.h>
#include <memory.h>
#include <stdint.h>
//类型名称的别名
using u_char = unsigned char;
using ngx_uint_t = unsigned int;

//定义ngx内存池相关参数
const int ngx_pagesize = 4096;//默认页面大小4k
const int NGX_MAX_ALLOC_FROM_POOL = (ngx_pagesize - 1);//小块内存池最大空间
const int NGX_DEFAULT_POOL_SIZE = (16 * 1024);//16k
const int NGX_POOL_ALIGNMENT = 16;//按照16字节进行对齐
const int NGX_ALIGNMENT = sizeof(unsigned long);


//定义ngx内存池用到的一些数据结构
typedef void (*ngx_pool_cleanup_pt)(void* data);
struct ngx_pool_cleanup_s {
	ngx_pool_cleanup_pt handler;
	void* data;              //指向要清除的数据  
	ngx_pool_cleanup_s* next;   //下一个cleanup callback  
};

struct ngx_pool_large_s
{
	ngx_pool_large_s* next;
	void* alloc;
};

struct ngx_pool_s;//前置声明
struct ngx_pool_data_t /* 内存池数据结构模块 */
{
	u_char* last;/* 当前内存分配的结束位置，即下一段可分配内存的起始位置 */
	u_char* end;/* 内存池的结束位置 */
	ngx_pool_s* next;/*内存池是一个链表结构，指向下一个内存池 */
	ngx_uint_t failed;/* 记录内存池内存分配失败的次数，反映内存池到底还是否有可用空间 */
};  /* 维护内存池的数据块 */

struct ngx_pool_s/* 内存池的管理模块，即内存池头部结构 */
{
	ngx_pool_data_t d;    /* 内存池的数据块 */
	size_t max;  /* 内存池数据块的最大值 */
	ngx_pool_s* current;/* 指向当前内存池 */
	ngx_pool_large_s* large;/* 大块内存链表，即分配空间超过 max 的内存 */
	ngx_pool_cleanup_s* cleanup;/* 析构函数，释放内存池 */
};


//把数值d上调到临近的a的倍数
#define ngx_alian(d,a)  (((d)+(a-1)) & ~(a-1))

#define ngx_align_ptr(p,a) \
(u_char *)(((uintptr_t)(p)+((uintptr_t)a-1))&~((uintptr_t)a-1))


#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


/*由于nginx中每一个连接都开辟一个内存池，因此ngx_pool不考虑线程安全问题*/
class NgxMemoryPool
{
public:
	NgxMemoryPool(size_t size);//构造函数，在其中进行了ngx_create_pool操作
	~NgxMemoryPool();//析构函数，在其中进行了ngx_destroy_pool操作
	bool ngx_create_success();//返回内存池是否创建成功
	void* ngx_palloc(size_t size);//考虑内存对齐的内存分配
	void* ngx_pnalloc(size_t size);//不考虑内存对齐的内存分配
	void* ngx_pcalloc(size_t size);//调用ngx_palloc，且将分配的内存初始化为0
	void ngx_pfree(void* p);//释放大块内存
	void ngx_reset_pool();//内存重置函数
	ngx_pool_cleanup_s* ngx_pool_cleanup_add(size_t size);//添加回调清理操作函数
private:
	ngx_pool_s* pool_;//指向ngx内存池的入口指针
	void* ngx_create_pool(size_t size);//创建指定size大小的内存池，但小块内存池不超过1个页面的大小
	void ngx_destroy_pool();//内存池的销毁函数
	void* ngx_palloc_small(size_t size, ngx_uint_t align);//小块内存分配
	void* ngx_palloc_large(size_t size);//大块内存分配
	void* ngx_palloc_block(size_t size);//分配新的小块内存池
};

#endif//__NGXPOOL_H__

