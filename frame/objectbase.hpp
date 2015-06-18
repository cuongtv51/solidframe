// frame/objectbase.hpp
//
// Copyright (c) 2014 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef SOLID_FRAME_OBJECTBASE_HPP
#define SOLID_FRAME_OBJECTBASE_HPP

#include <vector>

#include "frame/common.hpp"
#include "frame/event.hpp"
#include "utility/dynamictype.hpp"
#include "utility/dynamicpointer.hpp"
#include "system/atomic.hpp"

namespace solid{
namespace frame{

class Manager;
class Service;
class ReactorBase;
class Object;
class CompletionHandler;

class ObjectBase: public Dynamic<ObjectBase>{
public:
	//! Get the id of the object
	IndexT id() const;
	
	//! Virtual destructor
	virtual ~ObjectBase();
	
	UidT const& runId()const;
	
	bool isAcceptingEvents()const;
	
protected:
	friend class Service;
	friend class Manager;
	friend class ReactorBase;
	
	//! Constructor
	ObjectBase();
	
	//! Grab the signal mask eventually leaving some bits set- CALL this inside lock!!
	size_t grabSignalMask(const size_t _leave = 0);
	
	void unregister(Manager &_rm);
	bool isRegistered()const;
	virtual void doStop(Manager &_rm);
	
	bool notify(const size_t _smask);
	
	void disableVisits(Manager &_rm);
private:
	void id(const IndexT &_fullid);
	
	void runId(UidT const& _runid);
	void prepareSpecific();
	
	void stop(Manager &_rm);
private:
	ATOMIC_NS::atomic<IndexT>	fullid;
	UidT						runid;
	ATOMIC_NS::atomic<size_t>	smask;
};

inline IndexT ObjectBase::id()	const {
	return fullid;
}

inline bool ObjectBase::isRegistered()const{
	return fullid != static_cast<IndexT>(-1);
}

inline void ObjectBase::id(IndexT const &_fullid){
	fullid = _fullid;
}

inline UidT const& ObjectBase::runId()const{
	return runid;
}

inline void ObjectBase::runId(UidT const& _runid){
	runid = _runid;
}

inline size_t ObjectBase::grabSignalMask(const size_t _leave/* = 0*/){
	return smask.fetch_and(_leave/*, ATOMIC_NS::memory_order_seq_cst*/);
}

inline bool ObjectBase::notify(const size_t _smask){
	const size_t osm = smask.fetch_or(_smask/*, ATOMIC_NS::memory_order_seq_cst*/);
	return (_smask | osm) != osm;
}

}//namespace frame
}//namespace solid


#endif
