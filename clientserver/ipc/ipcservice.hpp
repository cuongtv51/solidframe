/* Declarations file ipcservice.hpp
	
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

#ifndef IPCSERVICE_H
#define IPCSERVICE_H

#include "clientserver/core/service.hpp"
#include "clientserver/core/command.hpp"
#include "clientserver/ipc/connectoruid.hpp"

namespace serialization{
namespace bin{
class RTTIMapper;
}
}

struct SockAddrPair;

namespace clientserver{

namespace tcp{
class Channel;
class Station;
}

namespace udp{
class Station;
class Talker;
}


namespace ipc{

class Connection;
class Talker;
class ProcessConnector;
class IOData;
struct Buffer;
struct ConnectorUid;

class Service;

//! A Inter Process Communication service
/*!
	<b>Overview:</b><br>
	- In its current state it only uses UDP for communication.
	- It can only send/receive command objects (objects of a type
	derived from clientserver::Command).
	- It uses non portable, binary serialization, aiming for speed
	not for versatility.
	- For udp communication uses connectors which resembles somehow
	the tcp connections.
	- There can be at most one connector linking two processes, and it 
	is created when first send something.
	- More than one connectors are handled by a single 
	clientserver::udp::Talker. There is a base connector used for both
	communication and accepting/creating new connectors. Also new talkers
	can be created when the number of connectors increases.
	- Because there can be only one connector to a peer process, and 
	commands can be quite big (i.e. commands sending streams) a command
	multiplexing algorithm is implemented.
	- An existing connector can be identified by it unique id: 
	clientserver::ipc::ConnectorUid or by its base address (inetaddr and port).
	- The ipc service should (i.e. it is a bug if it doesnt) ensure that
	 a response (or a command sent using SameConnectorFlag) will not
	 be sent if the a peer process restart is detected.
	
	<b>Usage:</b><br>
	- On server init, add the base taker to the ipc::Service
	- Then make your commands serializable
	- Send commands using a ipc::Service::sendCommand method.
	
	<b>Notes:</b><br>
	- Despite it's simple interface, the ipc service is quite complex
	software because it must ensure reliable communication over udp,
	emulating somehow the tcp.
	- There is no implementation of keep alive, so a peer process restart
	will only be detected when some communication happens in either way.
	- The ipc library is a nice example of how powerfull and flexible is the
	asynchrounous communication engine.
	- When needed, new talkers will be created with ports values starting 
	incrementally from baseaddress port + 1.
*/
class Service: public clientserver::Service{
public:
	enum {
		SameConnectorFlag = 1, //!< Do not send command to a restarted peer process
		ResponseFlag	= SameConnectorFlag //!< The sent command is a response
	};
	//! Destructor
	~Service();
	//!Send a command (usually a response) to a peer process using a previously saved ConnectorUid
	/*!
		The command is send only if the connector exists. If the peer process,
		restarts the command is not sent.
		\param _rconid A previously saved connectionuid
		\param _pcmd A CmdPtr with the command to be sent.
		\param _flags (Optional) Not used for now
	*/
	int sendCommand(
		const ConnectorUid &_rconid,//the id of the process connector
		clientserver::CmdPtr<Command> &_pcmd,//the command to be sent
		uint32	_flags = 0
	);
	//!Send a command to a peer process using it's base address.
	/*!
		The base address of a process is the address on which the process listens for new UDP connections.
		If the connection does not already exist, it will be created.
		\param _rsap The base socket address of the peer.
		\param _pcmd The command.
		\param _rcondid An output value, which on success will contain the uid of the connector.
		\param _flags (Optional) Not used for now
	*/
	int sendCommand(
		const SockAddrPair &_rsap,
		clientserver::CmdPtr<Command> &_pcmd,//the command to be sent
		ConnectorUid &_rconid,
		uint32	_flags = 0
	);
	//!Send a command to a peer process using it's base address.
	/*!
		The base address of a process is the address on which the process listens for new UDP connections.
		If the connection does not already exist, it will be created.
		\param _rsap The base socket address of the peer.
		\param _pcmd The command.
		\param _flags (Optional) Not used for now
	*/
	int sendCommand(
		const SockAddrPair &_rsap,
		clientserver::CmdPtr<Command> &_pcmd,//the command to be sent
		uint32	_flags = 0
	);
	//! Not used for now - will be used when ipc will use tcp connections
	int insertConnection(
		Server &_rs,
		clientserver::tcp::Channel *_pch
	);
	//! Not used for now - will be used when ipc will use tcp connections
	int insertListener(
		Server &_rs,
		const AddrInfoIterator &_rai
	);
	//! Use this method to add the base talker
	/*!
		Should be called only once at server initation.
		\param _rs Reference to server
		\param _rai 
	*/
	int insertTalker(
		Server &_rs, 
		const AddrInfoIterator &_rai,
		const char *_node,
		const char *_svc
	);
	//! Not used for now - will be used when ipc will use tcp connections
	int insertConnection(
		Server &_rs,
		const AddrInfoIterator &_rai,
		const char *_node,
		const char *_svc
	);
	//! Not used for now - will be used when ipc will use tcp connections
	int removeConnection(Connection &);
	//! Called by a talker when it's destroyed
	int removeTalker(Talker&);
	//! Returns the value of the base port as set for the basetalker
	int basePort()const;
protected:
	int execute(ulong _sig, TimeSpec &_rtout);
	Service(serialization::bin::RTTIMapper&);
	virtual void pushTalkerInPool(clientserver::Server &_rs, clientserver::udp::Talker *_ptkr) = 0;
private:
	friend class Talker;
	int doSendCommand(
		const SockAddrPair &_rsap,
		clientserver::CmdPtr<Command> &_pcmd,//the command to be sent
		ConnectorUid *_pconid,
		uint32	_flags = 0
	);
	int acceptProcess(ProcessConnector *_ppc);
	void disconnectProcess(ProcessConnector *_ppc);
	void disconnectTalkerProcesses(Talker &);
	int16 createNewTalker(uint32 &_tkrpos, uint32 &_tkruid);
	int16 computeTalkerForNewProcess();
private:
	struct Data;
	friend struct Data;
	Data	&d;
};

inline int Service::sendCommand(
	const SockAddrPair &_rsap,
	clientserver::CmdPtr<Command> &_pcmd,//the command to be sent
	ConnectorUid &_rconid,
	uint32	_flags
){
	return doSendCommand(_rsap, _pcmd, &_rconid, _flags);
}

inline int Service::sendCommand(
	const SockAddrPair &_rsap,
	clientserver::CmdPtr<Command> &_pcmd,//the command to be sent
	uint32	_flags
){
	return doSendCommand(_rsap, _pcmd, NULL, _flags);
}

}//namespace ipc
}//namespace clientserver

#endif