#include "solid/frame/manager.hpp"
#include "solid/frame/scheduler.hpp"
#include "solid/frame/service.hpp"

#include "solid/frame/aio/aioresolver.hpp"

#include "solid/frame/ipc/ipcservice.hpp"
#include "solid/frame/ipc/ipcconfiguration.hpp"

#include "ipc_echo_messages.hpp"

#include <iostream>

using namespace solid;
using namespace std;

using AioSchedulerT = frame::Scheduler<frame::aio::Reactor>;

//-----------------------------------------------------------------------------
//		Parameters
//-----------------------------------------------------------------------------
struct Parameters{
	Parameters():listener_port("0"), listener_addr("0.0.0.0"){}
	
	string			listener_port;
	string			listener_addr;
};

//-----------------------------------------------------------------------------

namespace ipc_echo_server{

template <class M>
void complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<M> &_rsent_msg_ptr,
	std::shared_ptr<M> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	SOLID_CHECK(not _rerror);
	
	if(_rrecv_msg_ptr){
		SOLID_CHECK(not _rsent_msg_ptr);
		ErrorConditionT err = _rctx.service().sendMessage(_rctx.recipientId(), std::move(_rrecv_msg_ptr));
		
		SOLID_CHECK(not err);
	}
	
	if(_rsent_msg_ptr){
		SOLID_CHECK(not _rrecv_msg_ptr);
	}
}

template <typename T>
struct MessageSetup{
	void operator()(frame::ipc::serialization_v1::Protocol &_rprotocol, const size_t _protocol_idx, const size_t _message_idx){
		_rprotocol.registerType<T>(complete_message<T>, _protocol_idx, _message_idx);
	}
};


}//namespace

//-----------------------------------------------------------------------------

bool parseArguments(Parameters &_par, int argc, char *argv[]);

//-----------------------------------------------------------------------------
//		main
//-----------------------------------------------------------------------------

int main(int argc, char *argv[]){
	Parameters p;
	
	if(!parseArguments(p, argc, argv)) return 0;
	
	{
		
		AioSchedulerT			scheduler;
		
		
		frame::Manager			manager;
		frame::ipc::ServiceT	ipcsvc(manager);
		ErrorConditionT			err;
		
		err = scheduler.start(1);
		
		if(err){
			cout<<"Error starting aio scheduler: "<<err.message()<<endl;
			return 1;
		}
		
		{
			frame::ipc::serialization_v1::Protocol	*proto = new frame::ipc::serialization_v1::Protocol;
			frame::ipc::Configuration				cfg(scheduler, proto);
			
			ipc_echo::ProtoSpecT::setup<ipc_echo_server::MessageSetup>(*proto);
			
			cfg.listener_address_str = p.listener_addr;
			cfg.listener_address_str += ':';
			cfg.listener_address_str += p.listener_port;
			
			cfg.connection_start_state = frame::ipc::ConnectionState::Active;
			
			err = ipcsvc.reconfigure(std::move(cfg));
			
			if(err){
				cout<<"Error starting ipcservice: "<<err.message()<<endl;
				manager.stop();
				return 1;
			}
			{
				std::ostringstream oss;
				oss<<ipcsvc.configuration().listenerPort();
				cout<<"server listens on port: "<<oss.str()<<endl;
			}
		}
		
		cout<<"Press any char and ENTER to stop: ";
		char c;
		cin>>c;
		
		manager.stop();
	}
	return 0;
}

//-----------------------------------------------------------------------------

bool parseArguments(Parameters &_par, int argc, char *argv[]){
	if(argc == 2){
		size_t			pos;
		
		_par.listener_addr = argv[1];
		
		pos = _par.listener_addr.rfind(':');
		
		if(pos != string::npos){
			_par.listener_addr[pos] = '\0';
			
			_par.listener_port.assign(_par.listener_addr.c_str() + pos + 1);
			
			_par.listener_addr.resize(pos);
		}
	}
	return true;
}

