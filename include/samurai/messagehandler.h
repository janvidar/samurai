/*
 * Copyright (C) 2001-2007 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_QUICKDC_MESSAGE_HANDLER_H
#define HAVE_QUICKDC_MESSAGE_HANDLER_H

#include <deque>
#include <vector>


namespace Samurai {


// A standalone postMessage.
void postMessage(size_t messageID, void* data, size_t arg1, size_t arg2);



/**
 * Messages used in the message handler.
 */
class Message {
	public:
		Message(size_t id, void* data, size_t arg1, size_t arg2)
			: m_id(id)
			, m_data(data)
			, m_arg1(arg1)
			, m_arg2(arg2)
		{
		}
		
	public:
		size_t m_id;
		void*  m_data;
		size_t m_arg1;
		size_t m_arg2;
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
		
		void postMessage(size_t id, void* data, size_t arg1, size_t arg2);
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
