/**************************************************************************
 * File name: sgi_pool.h
 * Description: The file imitates the secondary memory allocator of SGI STL 
				and contains the declaration and definition of the allocator.
 * Version: 1.0
 * Author: Fuming Liu
 * Date: July 10, 2022
 **************************************************************************/

#ifndef __SGIPOOL_H__
#define __SGIPOOL_H__
#include <mutex>//用于保证内存分配器的线程安全
#include <stdlib.h>
#include <iostream>

/*
* 由于SGI STL的二级空间配置器使用了template编程，因此模板类的声明和实现都应写在头文件中
* 由于空间配置器为容器提供服务，而容器产生的某个对象很有可能在多个线程中被操作，因此空间配置器需要考虑线程安全问题
*/


//封装malloc和free操作，可以设置OOM释放内存的回调函数，尝试是否能继续分配成功，否则抛出异常
template <int __inst>
class __malloc_alloc_template 
{

private:

	static void* _S_oom_malloc(size_t __n)
	{
		void (*__my_malloc_handler)();
		void* __result;

		for (;;) {
			__my_malloc_handler = __malloc_alloc_oom_handler;
			if (0 == __my_malloc_handler) { throw std::bad_alloc(); }
			(*__my_malloc_handler)();
			__result = malloc(__n);
			if (__result) return(__result);
		}
	}
	static void* _S_oom_realloc(void* __p, size_t __n)
	{
		void (*__my_malloc_handler)();
		void* __result;

		for (;;) {
			__my_malloc_handler = __malloc_alloc_oom_handler;
			if (0 == __my_malloc_handler) {throw std::bad_alloc(); }
			(*__my_malloc_handler)();
			__result = realloc(__p, __n);
			if (__result) return(__result);
		}
	}
	static void (*__malloc_alloc_oom_handler)();//类外初始化

public:

	static void* allocate(size_t __n)
	{
		void* __result = malloc(__n);
		if (0 == __result) __result = _S_oom_malloc(__n);
		return __result;
	}

	static void deallocate(void* __p, size_t /* __n */)
	{
		free(__p);
	}

	static void* reallocate(void* __p, size_t /* old_sz */, size_t __new_sz)
	{
		void* __result = realloc(__p, __new_sz);
		if (0 == __result) __result = _S_oom_realloc(__p, __new_sz);
		return __result;
	}

	static void (*__set_malloc_handler(void (*__f)()))()
	{
		void (*__old)() = __malloc_alloc_oom_handler;
		__malloc_alloc_oom_handler = __f;
		return(__old);
	}

};
template <int __inst>
void (*__malloc_alloc_template<__inst>::__malloc_alloc_oom_handler)() = 0;

typedef __malloc_alloc_template<0> malloc_alloc;

template<typename T>
class MyAllocator
{
public:
	using value_type = T;//容器使用内存分配器时，会用value_type来获取元素的类型
	//分配器的默认构造函数和拷贝构造函数，容器使用分配器时会调用
	/*
	constexpr是C++ 11标准提出的关键字，
	用于指明其后是一个常量（或者常量表达式），编译器在编译程序时可以顺带将其结果计算出来，
	而无需等到程序运行阶段，这样的优化可以极大地提高程序的执行效率。
	const侧重于修饰变量只读，而constexpr告诉编译器该量是常量，编译时即可确定
	*/
	/*
	* noexcept用于告诉编译器该函数不会抛出异常，编译器就会执行某些特殊的操作来优化编译
	*/
	constexpr MyAllocator() noexcept{}
	constexpr MyAllocator(const MyAllocator&) noexcept = default;
	template<class Other>
	constexpr MyAllocator(const MyAllocator<Other>&) noexcept{}

	T* allocate(size_t __n)//开辟内存
	{
		std::cout << "进入到空间配置器的allocate函数" << std::endl;
		__n *= sizeof(T);
		void* __ret = 0;
		if (__n > (size_t)_MAX_BYTES)
		{
			__ret = malloc_alloc::allocate(__n);
		}
		else 
		{
			_Obj* volatile* __my_free_list
				= _S_free_list + _S_freelist_index(__n);

			std::lock_guard<std::mutex> guard(mtx);//智能锁，出作用域自动释放锁
			_Obj* __result = *__my_free_list;
			if (__result == 0)
				__ret = _S_refill(_S_round_up(__n));
			else {
				*__my_free_list = __result->_M_free_list_link;
				__ret = __result;
			}
		}
		return static_cast<T*>(__ret);
	}
	void deallocate(void* __p, size_t __n)//释放内存
	{
		if (__n > (size_t)_MAX_BYTES)
			malloc_alloc::deallocate(__p,__n);
		else 
		{
			_Obj* volatile* __my_free_list
				= _S_free_list + _S_freelist_index(__n);
			_Obj* __q = (_Obj*)__p;

			std::lock_guard<std::mutex> guard(mtx);//智能锁，出作用域自动释放锁
			__q->_M_free_list_link = *__my_free_list;
			*__my_free_list = __q;
			// lock is released here
		}
	}
	void* reallocate(void* __p, size_t __old_sz, size_t __new_sz)//扩容或缩容
	{
		void* __result;
		size_t __copy_sz;

		if (__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t)_MAX_BYTES) {
			return(realloc(__p, __new_sz));
		}
		if (_S_round_up(__old_sz) == _S_round_up(__new_sz)) return(__p);
		__result = allocate(__new_sz);
		__copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
		memcpy(__result, __p, __copy_sz);
		deallocate(__p, __old_sz);
		return(__result);
	}
	void construct(T* __p, const T& val)
	{
		new(__p) T(val);//使用了placement new在指定内存处调用对象的构造函数
	}
	void destroy(T* __p)
	{
		__p->~T();
	}
private:
	static std::mutex mtx;//考虑线程安全，用户线程互斥操作
	enum { _ALIGN = 8 };//自由链表从8字节开始，以8字节为对齐方式
	enum { _MAX_BYTES = 128 };//内存池最大的chunk块
	enum { _NFREELISTS = 16 }; //自由链表的个数

	static char* _S_start_free;//备用池的开始位置
	static char* _S_end_free;//备用池的结束位置
	static size_t _S_heap_size;//累计向OS申请的内存总量

	//使用embraced-pointer，其中_M_free_list_link保存下一个chunk块的地址，节省内存空间
	union _Obj 
	{
		union _Obj* _M_free_list_link;
		char _M_client_data[1];
	};

	static _Obj* volatile _S_free_list[_NFREELISTS];//存储自由链表数组的起始地址,volatile防止在多线程中被线程缓存

	static size_t _S_round_up(size_t __bytes)//辅助函数，用于将传入的字节上调至最近的8倍数值
	{
		return (((__bytes)+(size_t)_ALIGN - 1) & ~((size_t)_ALIGN - 1));
	}
	static size_t _S_freelist_index(size_t __bytes)//辅助函数，用于计算传入字节数对应自由链表中的编号
	{
		return (((__bytes)+(size_t)_ALIGN - 1) / (size_t)_ALIGN - 1);
	}

	//把分配好的chunk块进行连接，挂到链表上
	static void* _S_refill(size_t __n)
	{
		int __nobjs = 20;
		char* __chunk = _S_chunk_alloc(__n, __nobjs);
		_Obj* volatile* __my_free_list;
		_Obj* __result;
		_Obj* __current_obj;
		_Obj* __next_obj;
		int __i;

		if (1 == __nobjs) return(__chunk);
		__my_free_list = _S_free_list + _S_freelist_index(__n);

		/* Build free list in chunk */
		__result = (_Obj*)__chunk;
		*__my_free_list = __next_obj = (_Obj*)(__chunk + __n);
		for (__i = 1; ; __i++) {
			__current_obj = __next_obj;
			__next_obj = (_Obj*)((char*)__next_obj + __n);
			if (__nobjs - 1 == __i) {
				__current_obj->_M_free_list_link = 0;
				break;
			}
			else {
				__current_obj->_M_free_list_link = __next_obj;
			}
		}
		return(__result);
	}


	//分配自由链表chunk块
	static char* _S_chunk_alloc(size_t __size,int& __nobjs)
	{
		char* __result;
		size_t __total_bytes = __size * __nobjs;
		size_t __bytes_left = _S_end_free - _S_start_free;

		if (__bytes_left >= __total_bytes) 
		{
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}
		else if (__bytes_left >= __size) 
		{
			__nobjs = (int)(__bytes_left / __size);
			__total_bytes = __size * __nobjs;
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}
		else
		{
			size_t __bytes_to_get =
				2 * __total_bytes + _S_round_up(_S_heap_size >> 4);
			// Try to make use of the left-over piece.
			if (__bytes_left > 0)
			{
				_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__bytes_left);

				((_Obj*)_S_start_free)->_M_free_list_link = *__my_free_list;
				*__my_free_list = (_Obj*)_S_start_free;
			}
			_S_start_free = (char*)malloc(__bytes_to_get);
			if (nullptr == _S_start_free)
			{
				size_t __i;
				_Obj* volatile* __my_free_list;
				_Obj* __p;
				for (__i = __size; __i <= (size_t)_MAX_BYTES; __i += (size_t)_ALIGN)
				{
					__my_free_list = _S_free_list + _S_freelist_index(__i);
					__p = *__my_free_list;
					if (0 != __p)
					{
						*__my_free_list = __p->_M_free_list_link;
						_S_start_free = (char*)__p;
						_S_end_free = _S_start_free + __i;
						return(_S_chunk_alloc(__size, __nobjs));
						// Any leftover piece will eventually make it to the
						// right free list.
					}
				}
				_S_end_free = 0;	// In case of exception.
				_S_start_free = (char*)malloc(__bytes_to_get);

			}
			_S_heap_size += __bytes_to_get;
			_S_end_free = _S_start_free + __bytes_to_get;
			return(_S_chunk_alloc(__size, __nobjs));
		}
	}
};

template<typename T>
std::mutex MyAllocator<T>::mtx;;//静态成员变量，在类外进行初始化

template<typename T>
char* MyAllocator<T>::_S_start_free = nullptr;//静态成员变量，在类外进行初始化

template<typename T>
char* MyAllocator<T>::_S_end_free = nullptr;//静态成员变量，在类外进行初始化

template<typename T>
size_t MyAllocator<T>::_S_heap_size = 0;//静态成员变量，在类外进行初始化

template<typename T>
typename MyAllocator<T>::_Obj* volatile MyAllocator<T>::_S_free_list[_NFREELISTS] = //静态成员变量，在类外进行初始化
{ nullptr,nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };//16个自由链表都初始化为空指针



#endif//__SGIPOOL_H__
//End
