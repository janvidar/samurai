/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_MESSAGE_HANDLER_H
#define HAVE_QUICKDC_MESSAGE_HANDLER_H

#include <deque>
#include <vector>


namespace Samurai {

enum MessageID {
	/* Control */
	MsgCoreCPUOverload        = 1001,   /**<< Don't accept new searches, don't compress transfers, etc. */
	MsgCoreCPUOK              = 1002,   /**<< Clear overload flag */
	
	/* Socket monitor - internals */
	MsgSocketMonitorAdd       = 2000,   /**<< Add a socket to the socket monitor */
	MsgSocketMonitorModify    = 2001,   /**<< Modify a socket in the socket monitor */
	MsgSocketMonitorRemove    = 2002,   /**<< Remove a socket from the socket monitor */
	MsgSocketMonitorDelete    = 2003,   /**<< Remove a socket from the socket monitor, and then delete it. */
	
	/* Config */
	MsgConfigUpdated          = 3000,   /**<< Make sure we send updated info whenever possible */
	MsgConfigNetworkUpdated   = 3001,   /**<< Networking information has changed, need to reapply configuration */
	
	/* Share related */
	MsgShareChanged           = 4001,   /**<< Share size have changed, all hubs should be notified */
	MsgShareRebuilding        = 4002,   /**<< Share database is rebuilding */
	MsgShareRebuilt           = 4003,   /**<< Share database is rebuilt */
	
	/* Hash related */
	MsgHashJobStarted         = 5001,   /**<< Hash job started */
	MsgHashJobStopped         = 5002,   /**<< Hash job stopped */
	
	/* Connection related */
	MsgConnectionAccepted     = 6000,   /**<< A connection has been accepted */
	MsgConnectionDropped      = 6001,   /**<< A connection has been dropped */
	
	/* Transfer related */
	MsgTransferStarted        = 7000,   /**<< A transfer job has started */
	MsgTransferStopped        = 7001,   /**<< A transfer job has stopped - and should be deleted */
	
	/* Local hub related */
	MsgHubUserConnected       = 8000,   /**<< A user connected to our local hub */
	MsgHubUserDisconnected    = 8001,   /**<< A user disconnected from our local hub */
	MsgHubUserLoggedIn        = 8002,   /**<< A user logged in to our local hub */
	MsgHubUserRemoveUser      = 8003,   /**<< A user should be removed from the local hub. */
	MsgHubUserAppendUser      = 8004,   /**<< A user should be added to the local hub. */
	MsgHubUserDelayedQuit     = 8005,   /**<< A user quit (usually error), post a message and process it later */
	
	/* Hub client related */
	MsgHubCountChanged        = 9000,   /**<< Our hub count data has changed - Notify hubs */
	MsgHubStopUserTransfers   = 9001,   /**<< A hub says stop transfers to a given user */
	
	MsgLAST
};

// A standalone postMessage.
void postMessage(enum MessageID, void* data, size_t arg1, size_t arg2);



/**
 * Messages used in the message handler.
 */
class Message {
	public:
		Message(enum MessageID id_, void* data_, size_t arg1_, size_t arg2_) {
			id = id_;
			data = data_;
			arg1 = arg1_;
			arg2 = arg2_;
		}
		
	public:
		enum MessageID id;
		void* data;
		size_t arg1;
		size_t arg2;
};

class MessageListener {
	public:
		MessageListener();
		virtual ~MessageListener();
	
		/**
		 * Process a given message. Return TRUE if the message was processed, or
		 * FALSE if the message was not processed by the listener.
		 */
		virtual bool EventMessage(const Message*) = 0;
};

/**
 * The message handler is being processed very often, and messages queued to it will be processed
 * promptly by invoking the different message handlers.
 */
class MessageHandler {
	protected:
		MessageHandler();
		
		
	public:
		~MessageHandler();
		static MessageHandler* getInstance();
		
		void postMessage(enum MessageID, void* data, size_t arg1, size_t arg2);
		void process();
		
		void addMessageListener(MessageListener*);
		void removeMessageListener(MessageListener*);
		
	private:
		void handleMessage(Message*);
		
	private:
		std::deque<Message*> queue;
		std::deque<Message*> busy_queue;
		std::vector<MessageListener*> listener;
		bool busy;
};

} // namespace

#endif // HAVE_QUICKDC_MESSAGE_HANDLER_H
