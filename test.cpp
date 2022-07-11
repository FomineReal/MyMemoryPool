#define _CRT_SECURE_NO_WARNINGS

#include "ngx_pool.h"
#include "sgi_pool.h"
#include <iostream>
#include <vector>

typedef struct Data stData;//这个结构用于给ngx_pool测试分配大块内存时使用
struct Data
{
	char* ptr;
	FILE* pfile;
};

void fun1(void* p1)//ngx_pool的clean_up回调1，用于释放大块内存中维护的其他堆区内存
{
	char* p = static_cast<char*>(p1);
	free(p);
	std::cout << "free ptr memory" << std::endl;
}

void fun2(void* pf1)//ngx_pool的clean_up回调2，用于关闭大块内存中打开的文件描述符
{
	FILE* pf = static_cast<FILE*>(pf1);
	fclose(pf);
	std::cout << "close file" << std::endl;
}

void test01()//该函数用于测试ngx_pool
{
	NgxMemoryPool pool(512);//ngx_create_pool的代码逻辑实现在NgxMemoryPool的构造函数中
	if (!pool.ngx_create_success())//如果创建失败
	{
		std::cout << "ngx_create fail..." << std::endl;
		return;
	}
	//经过调试,pool中的max为480，现在申请128，因此pcalloc内部会调用ngx_palloc_small
	void* p1 = pool.ngx_pcalloc(128);//应该是从小块内存池分配而来
	if (nullptr == p1)
	{
		std::cout << "ngx_pcalloc 128 bytes fail..." << std::endl;
		return;
	}
	else
		std::cout << "成功从ngx_pool中分配了小块内存，内存地址p1: " <<p1<< std::endl;

	stData* p2 = (stData*)pool.ngx_palloc(512);//应该是从大块内存池分配而来
	if (nullptr == p2)
	{
		std::cout << "ngx_pcalloc 512 bytes fail..." << std::endl;
		return;
	}
	else
		std::cout << "成功从ngx_pool中分配了大块内存，内存地址p2: " << p2 << std::endl;
	//让大块内存维护一块堆区内存和一个文件资源
	p2->ptr = (char*)malloc(12);
	strcpy(p2->ptr, "hello world");
	p2->pfile = fopen("data.txt", "w");

	//绑定回调
	ngx_pool_cleanup_s* c1 = pool.ngx_pool_cleanup_add(sizeof(char*));
	c1->handler = fun1;
	c1->data = p2->ptr;
	ngx_pool_cleanup_s* c2 = pool.ngx_pool_cleanup_add(sizeof(FILE*));
	c2->handler = fun2;
	c2->data = p2->pfile;

	std::cout << "下面将自动调用cleanup的回调函数" << std::endl;
	return;
	//内存池的销毁在NgxMemoryPool的析构函数中完成，当NgxMemoryPool对象离开作用域，会自动进行内存池的销毁
}

void test02()//该函数用于测试移植的SGI的空间配置器
{
	std::vector<int, MyAllocator<int>> vec;//创建一个vector时，指定自己的空间配置器
	for (int i = 0; i < 20; ++i)
	{
		vec.push_back(i);
	}
	for (int i = 0; i < 20; ++i)
	{
		std::cout << vec[i] << " ";
	}
	std::cout << std::endl;
}

int main()
{
	test01();
	std::cout << "--------------------------" << std::endl;
	test02();
	return 0;
}