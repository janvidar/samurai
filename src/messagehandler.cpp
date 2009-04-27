/*
 * Copyright (C) 2001-2009 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/samurai.h>
#include <samurai/messagehandler.h>

static Samurai::MessageHandler* g_message_handler = 0;

Samurai::MessageHandler* Samurai::MessageHandler::getInstance()
{
	if (!g_message_handler)
		g_message_handler = new Samurai::MessageHandler();
	return g_message_handler;
}

void Samurai::postMessage(size_t id, void* data, size_t arg1, size_t arg2)
{
	Samurai::MessageHandler::getInstance()->postMessage(id, data, arg1, arg2);
}

Samurai::MessageHandler::MessageHandler() {
	busy = false;
}

Samurai::MessageHandler::~MessageHandler() {
	while (queue.size()) {
		Samurai::Message* msg = queue.back();
		delete msg;
		queue.pop_back();
	}
	
	while (busy_queue.size()) {
		Samurai::Message* msg = busy_queue.back();
		delete msg;
		busy_queue.pop_back();
	}

}
	
void Samurai::MessageHandler::postMessage(size_t id, void* data, size_t arg1, size_t arg2)
{
	// We should perhaps allocate the new message from inside a static buffer.
	// In that case we could ignore freeing up memory, but it is always uncertain what the
	// worst case of queued messages would look like.
	Samurai::Message* msg = new Samurai::Message(id, data, arg1, arg2);
	
	if (!busy)
	{
		queue.push_front(msg);
	}
	else
	{
		busy_queue.push_front(msg);
	}
}

void Samurai::MessageHandler::process() {
	busy = true;
	while (queue.size()) {
		Samurai::Message* msg = queue.back();
		handleMessage(msg);
		delete msg;
		queue.pop_back();
	}

	while (busy_queue.size()) {
		Samurai::Message* msg = busy_queue.back();
		busy_queue.pop_back();
		queue.push_front(msg);
	}
	
	busy = false;
}

void Samurai::MessageHandler::handleMessage(Samurai::Message* msg) {
	std::vector<Samurai::MessageListener*>::iterator it;
	for (it = listener.begin(); it != listener.end(); it++) {
		(*it)->EventMessage(msg);
	}
}

void Samurai::MessageHandler::addMessageListener(Samurai::MessageListener* handler) {
//	QDBG("Samurai::MessageHandler::addMessageListener(): ptr=%p", handler);
	listener.push_back(handler);
}

void Samurai::MessageHandler::removeMessageListener(Samurai::MessageListener* handler) {
	std::vector<Samurai::MessageListener*>::iterator it;
	for (it = listener.begin(); it != listener.end(); it++) {
		if ((*it) == handler) {
			listener.erase(it);
			return;
		}
	}
}

Samurai::MessageListener::MessageListener()
{
	Samurai::MessageHandler::getInstance()->addMessageListener(this);
}

Samurai::MessageListener::~MessageListener()
{
	Samurai::MessageHandler::getInstance()->removeMessageListener(this);
}



