#pragma once
#include <stdlib.h>
#include <pthread.h>
#define SLOTS_COUNT	256
#define	CPU_CACHE_ALIGMENT	64

#if(__GNUC__ * 100 + __GNUC_MINOR__ * 10 + __GNUC_PATCHLEVEL__ >= 410)
#define atomic_inc(x) __sync_add_and_fetch((x),1)
#define atomic_dec(x) __sync_sub_and_fetch((x),1)
#define atomic_add(x,y) __sync_add_and_fetch((x),(y))
#define atomic_sub(x,y) __sync_sub_and_fetch((x),(y))
typedef long atomic_t;
#define ATOMIC_INIT(i)  { (i) }
#define atomic_read(x) __sync_add_and_fetch((x),0)
#define atomic_set(x,y) __sync_lock_test_and_set((x),y)
#else 
typedef long atomic_t;
#define atomic_inc(x)	((*(x))++)
#define atomic_dec(x)	((*(x))--)
#define atomic_add(x,y) ((*(x))+=(y))
#define atomic_sub(x,y) ((*(x))-=(y))
#define ATOMIC_INIT(i)  { (i) }
#define atomic_read(x) 	(*(x))
#define atomic_set(x,y)	((*(x))=(y))
#endif 

struct threadsafe_counter
{
	long long lcounters[(CPU_CACHE_ALIGMENT/sizeof(long long))*SLOTS_COUNT];
};
static __thread long slot_id=-1;
static __thread pid_t thread_id=0;
inline int get_lcounter_offset()
{
	int offset=0;
	if(slot_id<0)
	{
		thread_id=pthread_self();
		slot_id=random()%SLOTS_COUNT;
	}
	offset=(CPU_CACHE_ALIGMENT/sizeof(long long))*slot_id;
	return offset;
}
inline void threadsafe_counter_add(struct threadsafe_counter* c, long long value)
{
	int offset=get_lcounter_offset();
	atomic_add(&(c->lcounters[offset]), value);
}
inline void threadsafe_counter_sub(struct threadsafe_counter* c, long long value)
{
	int offset=get_lcounter_offset();
	atomic_sub(&(c->lcounters[offset]), value);
}

inline long long threadsafe_counter_read(struct threadsafe_counter* c)
{
	int i=0, offset=0;
	long long value=0;
	for(i=0; i<SLOTS_COUNT; i++)
	{
		offset=i*CPU_CACHE_ALIGMENT/sizeof(long long);
		value+=atomic_read(&(c->lcounters[offset]));
	}
	return value;
}
inline void threadsafe_counter_set(struct threadsafe_counter* c, long long value)
{
	int i=0, offset=0;
	for(i=0; i<SLOTS_COUNT; i++)
	{
		offset=i*CPU_CACHE_ALIGMENT/sizeof(long long);
		atomic_set(&(c->lcounters[offset]), 0);
	}
	atomic_set(&(c->lcounters[0]), value);
}

