// solid/utility/src/dynamictype.cpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#include <mutex>
#include "solid/system/debug.hpp"
#include "solid/system/cassert.hpp"

#include "solid/system/mutualstore.hpp"
#include "solid/utility/dynamictype.hpp"
#include "solid/utility/dynamicpointer.hpp"
#include "solid/utility/common.hpp"

#include <vector>

#ifdef SOLID_USE_GNU_ATOMIC
#include <ext/atomicity.h>
#endif

#include <atomic>

namespace solid{


//---------------------------------------------------------------------
//		Shared
//---------------------------------------------------------------------

namespace{

typedef MutualStore<std::mutex>	MutexStoreT;

#ifdef SOLID_USE_SAFE_STATIC
MutexStoreT &mutexStore(){
	static MutexStoreT		mtxstore(3, 2, 2);
	return mtxstore;
}

// size_t specificId(){
// 	static const size_t id(Thread::specificId());
// 	return id;
// }

#else

MutexStoreT &mutexStoreStub(){
	static MutexStoreT		mtxstore(3, 2, 2);
	return mtxstore;
}

// size_t specificIdStub(){
// 	static const size_t id(Thread::specificId());
// 	return id;
// }


void once_cbk_store(){
	mutexStoreStub();
}

// void once_cbk_specific(){
// 	specificIdStub();
// }

MutexStoreT &mutexStore(){
	static boost::once_flag once = BOOST_ONCE_INIT;
	boost::call_once(&once_cbk_store, once);
	return mutexStoreStub();
}

// size_t specificId(){
// 	static boost::once_flag once = BOOST_ONCE_INIT;
// 	boost::call_once(&once_cbk_specific, once);
// 	return specificIdStub();
// }
	

#endif

std::mutex& global_mutex(){
	static std::mutex mtx;
	return mtx;
}

}//namespace


std::mutex &shared_mutex_safe(const void *_p){
	std::unique_lock<std::mutex>	lock(global_mutex());
	return mutexStore().safeAt(reinterpret_cast<size_t>(_p));
}
std::mutex &shared_mutex(const void *_p){
	return mutexStore().at(reinterpret_cast<size_t>(_p));
}

//---------------------------------------------------------------------
//----	DynamicPointer	----
//---------------------------------------------------------------------

void DynamicPointerBase::clear(DynamicBase *_pdyn){
	SOLID_ASSERT(_pdyn);
	if(!_pdyn->release()) delete _pdyn;
}

void DynamicPointerBase::use(DynamicBase *_pdyn){
	_pdyn->use();
}

//--------------------------------------------------------------------
//		DynamicBase
//--------------------------------------------------------------------

typedef std::atomic<size_t>			AtomicSizeT;

/*static*/ size_t DynamicBase::generateId(){
	static AtomicSizeT u(ATOMIC_VAR_INIT(0));
	return u.fetch_add(1/*, std::memory_order_seq_cst*/);
}


DynamicBase::~DynamicBase(){}

size_t DynamicBase::use(){
	return usecount.fetch_add(1/*, std::memory_order_seq_cst*/) + 1;;
}

//! Used by DynamicPointer to know if the object must be deleted
size_t DynamicBase::release(){
	return usecount.fetch_sub(1/*, std::memory_order_seq_cst*/) - 1;
}

/*virtual*/ bool DynamicBase::isTypeDynamic(const size_t _id){
	return false;
}

}//namespace solid
