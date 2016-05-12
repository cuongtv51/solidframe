// utility/workpool.hpp
//
// Copyright (c) 2007, 2008 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef UTILITY_WORKPOOL_HPP
#define UTILITY_WORKPOOL_HPP

#include "system/thread.hpp"
#include "system/mutex.hpp"
#include "system/condition.hpp"

#include "utility/common.hpp"
#include "utility/queue.hpp"

namespace solid{

//! Base class for every workpool workers
struct WorkerBase: Thread{
	uint32	wkrid;
};

//! Base class for workpool
struct WorkPoolBase{
	enum State{
		Stopped = 0,
		Stopping,
		Running
	};
	State state()const{
		return st;
	}
	void state(State _s){
		st = _s;
	}
	bool isRunning()const{
		return st == Running;
	}
	bool isStopping()const{
		return st == Stopping;
	}
protected:
	WorkPoolBase():st(Stopped), wkrcnt(0) {}
	State						st;
	int							wkrcnt;
	Condition					thrcnd;
	Condition					sigcnd;
	Mutex						mtx;
};

//! A controller structure for WorkPool
/*!
 * The WorkPool will call methods of this structure.
 * Inherit and implement the needed methods.
 * No need for virtualization as the Controller is
 * a template parameter for WorkPool.
 */
struct WorkPoolControllerBase{
	bool prepareWorker(WorkerBase &_rw){
		return true;
	}
	void unprepareWorker(WorkerBase &_rw){
	}
	void onPush(WorkPoolBase &){
	}
	void onMultiPush(WorkPoolBase &, size_t _cnd){
	}
	size_t onPopStart(WorkPoolBase &, WorkerBase &, size_t _maxcnt){
		return _maxcnt;
	}
	void onPopDone(WorkPoolBase &, WorkerBase &){
	}
	void onStop(){
	}
};

//! A template workpool
/*!
 * The template parameters are:<br>
 * J - the Job type to be processed by the workpool. I
 * will hold a Queue\<J\>.<br>
 * C - WorkPool controller, the workpool will call controller
 * methods on different ocasions / events<br>
 * W - Base class for workers. Specify this if you want certain data
 * kept per worker. The workpool's actual workers will publicly
 * inherit W.<br>
 */
template <class J, class C, class W = WorkerBase>
class WorkPool: public WorkPoolBase{
	typedef std::vector<J>		JobVectorT;
	typedef WorkPool<J, C, W>	ThisT;
	struct SingleWorker: W{
		SingleWorker(ThisT &_rw):rw(_rw){}
		void run(){
			if(!rw.enterWorker(*this)){
				return;
			}
			J	job;
			while(rw.pop(*this, job)){
				rw.execute(*this, job);
			}
			rw.exitWorker(*this);
		}
		ThisT	&rw;
	};
	struct MultiWorker: W{
		MultiWorker(ThisT &_rw, size_t _maxcnt):rw(_rw), maxcnt(_maxcnt){}
		void run(){
			if(!rw.enterWorker(*this)){
				return;
			}
			JobVectorT	jobvec;
			if(maxcnt == 0) maxcnt = 1;
			while(rw.pop(*this, jobvec, maxcnt)){
				rw.execute(*this, jobvec);
				jobvec.clear();
			}
			rw.exitWorker(*this);
		}
		ThisT	&rw;
		size_t	maxcnt;
	};
	
public:
	typedef ThisT	WorkPoolT;
	typedef C		ControllerT;
	typedef W		WorkerT;
	typedef J		JobT;
	
	
	WorkPool(){
	}
	
	template <class T>
	WorkPool(T &_rt):ctrl(_rt){
	}
	
	~WorkPool(){
		stop(true);
	}
	
	ControllerT& controller(){
		return ctrl;
	}
	
	//! Push a new job
	void push(const JobT& _jb){
		mtx.lock();
		jobq.push(_jb);
		sigcnd.signal();
		ctrl.onPush(*this);
		mtx.unlock();
	}
	//! Push a table of jobs of size _cnt
	template <class I>
	void push(I _i, const I &_end){
		mtx.lock();
		size_t cnt(_end - _i);
		for(; _i != _end; ++_i){
			jobq.push(*_i);
		}
		sigcnd.broadcast();
		ctrl.onMultiPush(*this, cnt);
		mtx.unlock();
	}
	//! Starts the workpool, creating _minwkrcnt
	void start(ushort _minwkrcnt = 0){
		Locker<Mutex> lock(mtx);
		if(state() == Running){
			return;
		}
		if(state() != Stopped){
			doStop(lock, true);
		}
		wkrcnt = 0;
		state(Running);
		for(ushort i(0); i < _minwkrcnt; ++i){
			createWorker();
		}
	}
	//! Initiate workpool stop
	/*!
		It can block waiting for all workers to stop or just initiate stopping.
		The ideea is that when one have M workpools, it is faster to
		first initiate stop for all pools (wp[i].stop(false)) and then wait
		for actual stoping (wp[i].stop(true))
	*/
	void stop(bool _wait = true){
		Locker<Mutex>	lock(mtx);
		doStop(lock, _wait);
	}
	size_t size()const{
		return jobq.size();
	}
	bool empty()const{
		return jobq.empty();
	}
	void createWorker(){
		SOLID_ASSERT(!mtx.tryLock());
		++wkrcnt;
		if(ctrl.createWorker(*this, wkrcnt)){
		}else{
			--wkrcnt;
			thrcnd.broadcast();
		}
	}
	WorkerT* createSingleWorker(){
		return new SingleWorker(*this);
	}
	WorkerT* createMultiWorker(size_t _maxcnt){
		return new MultiWorker(*this, _maxcnt);
	}
	
private:
	friend struct SingleWorker;
	friend struct MultiWorker;
	void doStop(Locker<Mutex> &_lock, bool _wait){
		if(state() == Stopped) return;
		state(Stopping);
		sigcnd.broadcast();
		ctrl.onStop();
		if(!_wait) return;
		while(wkrcnt){
			thrcnd.wait(_lock);
		}
		state(Stopped);
	}
	bool pop(WorkerT &_rw, JobVectorT &_rjobvec, size_t _maxcnt){
		Locker<Mutex> lock(mtx);
		uint32 insertcount(ctrl.onPopStart(*this, _rw, _maxcnt));
		if(!insertcount){
			return true;
		}
		if(doWaitJob(lock)){
			do{
				_rjobvec.push_back(jobq.front());
				jobq.pop();
			}while(jobq.size() && --insertcount);
			ctrl.onPopDone(*this, _rw);
			return true;
		}
		return false;
	}
	
	bool pop(WorkerT &_rw, JobT &_rjob){
		Locker<Mutex> lock(mtx);
		if(ctrl.onPopStart(*this, _rw, 1) == 0){
			sigcnd.signal();
			return false;
		}
		if(doWaitJob(lock)){
			_rjob = jobq.front();
			jobq.pop();
			ctrl.onPopDone(*this, _rw);
			return true;
		}
		return false;
	}
	
	size_t doWaitJob(Locker<Mutex> &_lock){
		while(jobq.empty() && isRunning()){
			sigcnd.wait(_lock);
		}
		return jobq.size();
	}
	
	bool enterWorker(WorkerT &_rw){
		Locker<Mutex> lock(mtx);
		if(!ctrl.prepareWorker(_rw)){
			return false;
		}
		//++wkrcnt;
		thrcnd.broadcast();
		return true;
	}
	void exitWorker(WorkerT &_rw){
		mtx.lock();
		ctrl.unprepareWorker(_rw);
		--wkrcnt;
		SOLID_ASSERT(wkrcnt >= 0);
		thrcnd.broadcast();
		mtx.unlock();
	}
	void execute(WorkerT &_rw, JobT &_rjob){
		ctrl.execute(*this, _rw, _rjob);
	}
	void execute(WorkerT &_rw, JobVectorT &_rjobvec){
		ctrl.execute(*this, _rw, _rjobvec);
	}
private:
	Queue<JobT>					jobq;
	ControllerT					ctrl;
	
};

}//namespace solid

#endif



