/* Declarations file object.hpp
	
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

#ifndef CS_OBJECT_HPP
#define CS_OBJECT_HPP

#include "common.hpp"
#include "cmdptr.hpp"
class Mutex;
struct TimeSpec;
namespace foundation{

class Visitor;
class Manager;
class Service;
class ReadWriteService;
class ObjPtrBase;
class Command;

#ifndef USERVICEBITS
#define USERVICEBITS (sizeof(IndexTp) == 4 ? 5 : 8)
#endif

enum ObjectDefs{
	SERVICEBITCNT = USERVICEBITS,
	INDEXBITCNT	= sizeof(IndexTp) * 8 - SERVICEBITCNT,
};


//! A pseudo-active object class
/*!
	<b>Overview:</b><br>
	A pseudo-active object is one that altough it doesnt own a thread
	it can reside within a workpool an receive processor/thread time
	due to events.
	
	So an object:
	- can receive signals
	- can receive commands
	- must reside both on a static container (foundation::Service) and 
	an ActiveSet (usually foundation::SelectPool)
	- has an associated mutex which can be requested from the service
	- can be visited using a visitor<br>
	
	<b>Usage:</b><br>
	- Inherit the object and implement execute, adding code for grabing
	the signal mask and the commands under lock.
	- Add code so that the object gets registered on service and inserted
	into a proper workpool upon its creation.
	
	<b>Notes:</b><br>
	- Every object have an associated unique id (a pair of uint32) wich 
	uniquely identifies the object within the manager both on memory and on
	time. The solidground architecture and the ones built upon it MUST NOT allow
	access to object pointers, instead they do/should do permit limited access
	using the unique id: sigaling and sending commands through the manager interface.
	- Also an object will hold information about the thread in which it is currently
	executed, so that the signaling is fast.
*/
class Object{
public:
	typedef Command	CommandTp;
	//! Extracts the object index within service from an objectid
	static IndexTp computeIndex(IndexTp _fullid);
	//! Extracts the service id from an objectid
	static IndexTp computeServiceId(IndexTp _fullid);
	//! Constructor
	Object(IndexTp _fullid = 0UL);
	
	//getters:
	//! Get the id of the parent service
	IndexTp serviceid()const;
	//! Get the index of the object within service from an objectid
	IndexTp index()const;

	//! Get the id of the object
	IndexTp id()			const 	{return fullid;}
	
	//! Gets the id of the thread the object resides in
	void getThread(uint32 &_rthid, uint32 &_rthpos);
	//! Returns true if the object is signaled
	ulong signaled()			const 	{return smask;}
	
	ulong signaled(ulong _s)	const;
	
	//setters:
	//! Set the thread id
	void setThread(uint32 _thrid, uint32 _thrpos);
	
	//! Returns the state of the objec -a negative state means the object must be destroyed
	int state()	const 	{return crtstate;}
	/**
	 * Returns true if the signal should raise the object ASAP
	 * \param _smask The signal bitmask
	 */
	ulong signal(ulong _smask);
	//! Signal the object with a command
	virtual int signal(CmdPtr<Command> &_cmd);
	
	//! Executes the objects
	/*!
		This method is calle by basic objectpools with no support for
		events and timeouts
	*/
	virtual int execute();
	//! Executes the objects
	/*!
		This method is calle by selectpools with support for
		events and timeouts
	*/
	virtual int execute(ulong _evs, TimeSpec &_rtout);
	//! Acceptor method for different visitors
	virtual int accept(Visitor &_roi);
	//! This method will be called once by service when registering an object
	/*!
		Some objects may need faster access to their associated mutex, so they
		might want to keep a pointe to it.
	*/
	virtual void mutex(Mutex *_pmut);
protected:
	friend class Service;
	friend class ReadWriteService;
	friend class ObjPtrBase;
	//! Sets the current state.
	/*! If your object will implement a state machine (and in an asynchronous
	environments it most certanly will) use this base state setting it to
	somethin negative on destruction
	*/
	void state(int _st);
	//! Grab the signal mask eventually leaving some bits set- CALL this inside lock!!
	ulong grabSignalMask(ulong _leave = 0);
	//! Virtual destructor
	virtual ~Object();//only objptr base can destroy an object
private:
	//! Set the id
	void id(IndexTp _fullid);
	//! Set the id given the service id and index
	void id(IndexTp _srvid, IndexTp _ind);
private:
	IndexTp			fullid;
	ulong			smask;
	volatile uint32	thrid;//the current thread which (may) execute(s) the object
	volatile uint32	thrpos;//
	short			usecnt;//
	short			crtstate;// < 0 -> must die
};
#ifdef UINLINES
#include "src/object.ipp"
#endif
}//namespace

#endif