#include "solid/frame/manager.hpp"
#include "solid/frame/scheduler.hpp"
#include "solid/frame/service.hpp"

#include "solid/frame/aio/aioresolver.hpp"

#include "solid/frame/ipc/ipcservice.hpp"
#include "solid/frame/ipc/ipcconfiguration.hpp"

#include "ipc_file_messages.hpp"

#include <iostream>

using namespace solid;
using namespace std;

using AioSchedulerT = frame::Scheduler<frame::aio::Reactor>;

//-----------------------------------------------------------------------------
//		Parameters
//-----------------------------------------------------------------------------
struct Parameters{
	Parameters():port("3333"){}
	
	string			port;
};

//-----------------------------------------------------------------------------
namespace ipc_file_client{

template <class M>
void complete_message(
	frame::ipc::ConnectionContext &_rctx,
	std::shared_ptr<M> &_rsent_msg_ptr,
	std::shared_ptr<M> &_rrecv_msg_ptr,
	ErrorConditionT const &_rerror
){
	SOLID_CHECK(false);//this method should not be called
}

template <typename T>
struct MessageSetup{
	void operator()(frame::ipc::serialization_v1::Protocol &_rprotocol, const size_t _protocol_idx, const size_t _message_idx){
		_rprotocol.registerType<T>(complete_message<T>, _protocol_idx, _message_idx);
	}
};


}//namespace


namespace{
streampos stream_size(iostream &_rios){
	streampos pos = _rios.tellg();
	_rios.seekg(0, _rios.end);
	streampos endpos = _rios.tellg();
	_rios.seekg(pos);
	return endpos;
}
}

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
		
		frame::aio::Resolver	resolver;
		
		ErrorConditionT			err;
		
		err = scheduler.start(1);
		
		if(err){
			cout<<"Error starting aio scheduler: "<<err.message()<<endl;
			return 1;
		}
		
		err = resolver.start(1);
		
		if(err){
			cout<<"Error starting aio resolver: "<<err.message()<<endl;
			return 1;
		}
		
		{
			frame::ipc::serialization_v1::Protocol	*proto = new frame::ipc::serialization_v1::Protocol;
			frame::ipc::Configuration				cfg(scheduler, proto);
			
			ipc_file::ProtoSpecT::setup<ipc_file_client::MessageSetup>(*proto);
			
			cfg.name_resolve_fnc = frame::ipc::InternetResolverF(resolver, p.port.c_str());
			
			cfg.connection_start_state = frame::ipc::ConnectionState::Active;
			
			err = ipcsvc.reconfigure(std::move(cfg));
			
			if(err){
				cout<<"Error starting ipcservice: "<<err.message()<<endl;
				return 1;
			}
		}
		
		cout<<"Expect lines like:"<<endl;
		cout<<"quit"<<endl;
		cout<<"q"<<endl;
		cout<<"localhost l /home"<<endl;
		cout<<"localhost L /home/remote_user"<<endl;
		cout<<"localhost C /home/remote_user/remote_file ./local_file"<<endl;
		cout<<"localhost c /home/remote_user/remote_file /home/local_user/local_file"<<endl;
		
		while(true){
			string	line;
			getline(cin, line);
			
			if(line == "q" or line == "Q" or line == "quit"){
				break;
			}
			{
				string		recipient;
				size_t		offset = line.find(' ');
				if(offset != string::npos){
					
					recipient = line.substr(0, offset);
					
					std::istringstream iss(line.substr(offset + 1));
					
					char 			choice;
					
					iss>>choice;
					
					switch(choice){
						case 'l':
						case 'L':{
							std::string		path;
							iss>>path;
							ipcsvc.sendMessage(
								recipient.c_str(), make_shared<ipc_file::ListRequest>(std::move(path)),
								[](
									frame::ipc::ConnectionContext &_rctx,
									std::shared_ptr<ipc_file::ListRequest> &_rsent_msg_ptr,
									std::shared_ptr<ipc_file::ListResponse> &_rrecv_msg_ptr,
									ErrorConditionT const &_rerror
								){
									if(_rerror){
										cout<<"Error sending message to "<<_rctx.recipientName()<<". Error: "<<_rerror.message()<<endl;
										return;
									}
									SOLID_CHECK(not _rerror and _rsent_msg_ptr and _rrecv_msg_ptr);
									cout<<"File List from "<<_rctx.recipientName()<<" with "<<_rrecv_msg_ptr->node_dq.size()<<" items: {"<<endl;
									for(auto it: _rrecv_msg_ptr->node_dq){
										cout<<(it.second ? 'D' : 'F')<<": "<<it.first<<endl;
									}
									cout<<"}"<<endl;
								},
								0|frame::ipc::MessageFlags::WaitResponse
							);
							
							break;
						}
						
						case 'c':
						case 'C':{
							std::string 	remote_path, local_path;
							
							iss>>remote_path>>local_path;
							
							ipcsvc.sendMessage(
								recipient.c_str(), make_shared<ipc_file::FileRequest>(std::move(remote_path), std::move(local_path)),
								[](
									frame::ipc::ConnectionContext &_rctx,
									std::shared_ptr<ipc_file::FileRequest> &_rsent_msg_ptr,
									std::shared_ptr<ipc_file::FileResponse> &_rrecv_msg_ptr,
									ErrorConditionT const &_rerror
								){
									if(_rerror){
										cout<<"Error sending message to "<<_rctx.recipientName()<<". Error: "<<_rerror.message()<<endl;
										return;
									}
									
									SOLID_CHECK(not _rerror and _rsent_msg_ptr and _rrecv_msg_ptr);
									
									cout<<"Done copy from "<<_rctx.recipientName()<<":"<<_rrecv_msg_ptr->remote_path<<" to "<<_rrecv_msg_ptr->local_path<<": ";
									
									if(_rrecv_msg_ptr->remote_file_size != InvalidSize() and _rrecv_msg_ptr->remote_file_size == stream_size(_rrecv_msg_ptr->fs)){
										cout<<"Success("<<_rrecv_msg_ptr->remote_file_size<<")"<<endl;
									}else if(_rrecv_msg_ptr->remote_file_size == InvalidSize()){
										cout<<"Fail(no remote)"<<endl;
									}else{
										cout<<"Fail("<<stream_size(_rrecv_msg_ptr->fs)<<" instead of "<<_rrecv_msg_ptr->remote_file_size<<")"<<endl;
									}
								},
								0|frame::ipc::MessageFlags::WaitResponse
							);
							break;
						}
						default:
							cout<<"Unknown choice"<<endl;
							break;
					}
					
				}else{
					cout<<"No recipient specified. E.g:"<<endl<<"localhost:4444 Some text to send"<<endl;
				}
			}
		}
		
		manager.stop();
	}
	return 0;
}

//-----------------------------------------------------------------------------

bool parseArguments(Parameters &_par, int argc, char *argv[]){
	if(argc == 2){
		_par.port = argv[1];
	}
	return true;
}




