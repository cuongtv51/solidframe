/* Declarations file selectpool.hpp
	
	Copyright 2007, 2008 Valentin Palade 
	vipalade@gmail.com

	This file is part of SolidGround framework.

	SolidGround is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	SolidGround is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with SolidGround.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SELECTPOOL_HPP
#define SELECTPOOL_HPP

#include <deque>

#include "utility/workpool.hpp"
#include "utility/list.hpp"

#include "manager.hpp"
#include "activeset.hpp"


namespace foundation{

//! An active container for objects needing complex handeling
/*!
	<b>Overview:</b><br>
	Complex handeling means that objects can reside within the
	pool for as much as they want, and are given processor time
	as result of different events.
	
	<b>Usage:</b><br>
	- Use the SelectPool together with a selector, which will ensure
	object handeling at thread level.
	- Objects must implement "int execute(ulong _evs, TimeSpec &_rtout)" method.
*/
template <class Sel>
class SelectPool: public WorkPool<typename Sel::ObjectTp>, public ActiveSet{
public:
	typedef Sel										SelectorTp;
	typedef typename Sel::ObjectTp					JobTp;
	typedef SelectPool<Sel>							ThisTp;
	typedef WorkPool<typename Sel::ObjectTp>		WorkPoolTp;

private:
	typedef List<ulong>								ListTp;
	typedef std::pair<SelectorTp*,ListTp::iterator>	VecPairTp;
	typedef std::vector<VecPairTp>		 			SlotVecTp;

protected:
	struct SelectorWorker;

public://definition
	//! Constructor
	/*!
		\param _rm Reference to parent manager
		\param _maxthcnt The maximum count of threads that can be created
		\param _selcap The capacity of a selector - the total number
		of objects handled would be _maxthcnt * _selcap
	*/
	SelectPool(Manager &_rm, uint _maxthcnt, uint _selcap = 1024):rm(_rm),cap(0),selcap(_selcap){
		thrid = _rm.registerActiveSet(*this);
		thrid <<= 16;
		slotvec.reserve(_maxthcnt > 1024 ? 1024 : _maxthcnt);
	}
	
	//! Raise a thread from the pool
	void raise(uint _thid){
		cassert(_thid < slotvec.size());
		slotvec[_thid].first->signal();
	}
	
	//! Raise an object from the pool
	void raise(uint _thid, uint _thpos){
		cassert(_thid < slotvec.size());
		slotvec[_thid].first->signal(_thpos);
	}
	
	//! Raise all threads for stopping
	void raiseStop(){
		{
			Mutex::Locker	lock(this->mtx);
			for(typename SlotVecTp::iterator it(slotvec.begin()); it != slotvec.end(); ++it){
				it->first->signal();
			}
		}
		this->stop();
	}
	
	//! Set the pool id
	void poolid(uint _pid){
		Mutex::Locker	lock(this->mtx);
		thrid = _pid << 16;
	}
	
	//! Prepare the worker (usually thread specific data) - called internally
	void prepareWorker(){
		rm.prepareThread();
		doPrepareWorker();
	}
	
	//! Prepare the worker (usually thread specific data) - called internally
	void unprepareWorker(){
		doUnprepareWorker();
		rm.unprepareThread();
	}
	
	//! The run loop for every thread
	void run(SelectorWorker &_rwkr){
		_rwkr.sel.prepare();
		while(pop(_rwkr.sel,_rwkr.thrid) >= 0){
			_rwkr.sel.run();
		}
		_rwkr.sel.unprepare();
		unregisterSelector(_rwkr.sel, _rwkr.thrid);
	}
	
	//! The method for adding a new object to the pool
	/*!
		Method overwritten from WorkPoolTp, to add logic 
		for creating new workers.
	 */
	void push(const JobTp &_rjb){
		Mutex::Locker	lock(this->mtx);
		cassert(this->state != WorkPoolTp::Stopped);
		this->q.push(_rjb); this->sigcnd.signal();
		if(sgnlst.size()){
			slotvec[sgnlst.back()].first->signal();
		}else{
			createWorkers(1);//increase the capacity
		}
	}

protected:
	//! Inherit and add thread preparation - or use workpool plugin
	virtual void doPrepareWorker(){}
	
	//! Inherit and add thread unpreparation - or use workpool plugin
	virtual void doUnprepareWorker(){}

protected:
	//! The selector worker
	struct SelectorWorker: WorkPoolTp::Worker{
		SelectorWorker(){}
		~SelectorWorker(){}
		uint 		thrid;
		SelectorTp	sel;
	};
	
	//! The creator of workers
	/*!
		Observe the fact that the created workers type
		is WorkPool::GenericWorker\<SelectorWorker, ThisTp\>
	*/
	int createWorkers(uint _cnt){
		uint i(0);
		for(; i < _cnt; ++i){
			SelectorWorker *pwkr = this->template createWorker<SelectorWorker,ThisTp>(*this);
			if(!registerSelector(pwkr->sel, pwkr->thrid))
				pwkr->start();//do not wait
			else{ delete pwkr; break;}
		}
		return (int)i;
	}
	
	//! Register the selector of a worker 
	int registerSelector(SelectorTp &_rs, uint &_wkrid){
		if(_rs.reserve(selcap)) return BAD;
		_wkrid = thrid++;
		cap += _rs.capacity();
		uint wid = _wkrid & 0xffff;
		if(wid >= slotvec.size()){
			slotvec.push_back(VecPairTp(&_rs, sgnlst.insert(sgnlst.end(),wid)));
		}else{
			slotvec[wid] = VecPairTp(&_rs, sgnlst.insert(sgnlst.end(),wid));
		}
		return OK;
	}
	
	//! Unregister the selector of a worker 
	void unregisterSelector(SelectorTp &_rs, ulong _wkrid){
		Mutex::Locker	lock(this->mtx);
		cap -= _rs.capacity();
		slotvec[_wkrid & 0xffff] = VecPairTp(NULL, sgnlst.end());
	}
	
	//! Pop some objects into the selector
	int pop(SelectorTp &_rsel, uint _wkrid){
		Mutex::Locker lock(this->mtx);
		int tid = _wkrid & 0xffff;
		if(_rsel.full()){//can do nothing but ensure the worker will not be raised again
			doEnsureWorkerNotRaise(tid);
			if(this->q.size()) doTrySignalOtherWorker();
			return OK;
		}
		//the selector is not full
		if(slotvec[tid].second == sgnlst.end()){
			//register that we're not full so the selector can be signaled
			slotvec[tid].second = sgnlst.insert(sgnlst.end(), tid);
		}
		if((this->q.empty() && !_rsel.empty())){
			//the selector is busy with its connections
			return OK;
		}
		if(doWaitJob()){
			//we have at least one job
			do{
				_rsel.push(this->q.front(), _wkrid); this->q.pop();
			}while(this->q.size() && !_rsel.full());
			
			if(this->q.size()){//the job queue is not empty..
				doEnsureWorkerNotRaise(tid);
				doTrySignalOtherWorker();
			}
			if(this->state == WorkPoolTp::Running) return OK;
			return NOK;
		}
		return BAD;
	}

private:
	//! Make sure that the worker will not be raised again.
	inline void doEnsureWorkerNotRaise(int _thndx){
		if(slotvec[_thndx].second != sgnlst.end()){
			sgnlst.erase(slotvec[_thndx].second);
			slotvec[_thndx].second = sgnlst.end();
		}
	}
	
	//! Wait for a new object
	int doWaitJob(){
		while(this->q.empty() && this->state == WorkPoolTp::Running){
			this->sigcnd.wait(this->mtx);
		}
		return this->q.size();
	}
	
	//! Try to signal another worker
	inline void doTrySignalOtherWorker(){
		if(sgnlst.size()){
			//maybe some other worker can take the job
			raise(sgnlst.back());
		}else{
			//TODO: it might not be safe - create unneeded threads
			createWorkers(1);
		}
	}

private:
	Manager			&rm;
	ulong			cap;//the total count of objects already in pool
	uint			selcap;
	uint			thrid;
	ListTp			sgnlst;
	SlotVecTp		slotvec;
	
};

}//namesspace foundation

#endif

