/* Implementation file multiconnection.cpp
	
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
#ifndef AIO_MULTITALKER_HPP
#define AIO_MULTITALKER_HPP

#include "foundation/aio/aioobject.hpp"
#include "utility/stack.hpp"

class SocketAddress;
class SockAddrPair;

namespace foundation{

namespace aio{

namespace udp{

class MultiTalker: public Object{
public:
	MultiTalker(Socket *_psock = NULL);
	MultiTalker(const SocketDevice &_rsd);
	~MultiTalker();
	bool socketOk(uint _pos)const;
	int socketSend(uint _pos, const char* _pb, uint32 _bl, const SockAddrPair &_sap, uint32 _flags = 0);
	int socketRecv(uint _pos, char *_pb, uint32 _bl, uint32 _flags = 0);
	uint32 socketRecvSize(uint _pos)const;
	const SockAddrPair &socketRecvAddr(uint _pos) const;
	const uint64& socketSendCount(uint _pos)const;
	const uint64& socketRecvCount(uint _pos)const;
	bool socketHasPendingSend(uint _pos)const;
	bool socketHasPendingRecv(uint _pos)const;
	int socketLocalAddress(uint _pos, SocketAddress &_rsa)const;
	int socketRemoteAddress(uint _pos, SocketAddress &_rsa)const;
	void socketTimeout(uint _pos, const TimeSpec &_crttime, ulong _addsec, ulong _addnsec = 0);
	uint32 socketEvents(uint _pos)const;
	void socketErase(uint _pos);
	int socketInsert(Socket *_psock);
	int socketInsert(const SocketDevice &_rsd);
	int socketCreate4(const SockAddrPair &_sap);
	int socketCreate6(const SockAddrPair &_sap);
	int socketCreate(const AddrInfoIterator&);
	void socketRequestRegister(uint _pos);
	void socketRequestUnregister(uint _pos);
	int socketState(unsigned _pos)const;
	void socketState(unsigned _pos, int _st);
private:
	void reserve(uint _cp);//using only one allocation sets the pointers from the aioobject
	uint dataSize(uint _cp);
	uint newStub();
private:
	typedef Stack<uint>		PositionStackTp;
	PositionStackTp		posstk;
};

}//namespace udp

}//namespace aio

}//namespace foundation


#endif
