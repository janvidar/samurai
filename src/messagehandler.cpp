/*
 * Copyright (C) 2001-2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <algorithm>
#include <samurai/samurai.h>
#include <samurai/messagehandler.h>
#include <samurai/eventloop.h>
#include <memory>

Samurai::MessageHandler::MessageHandler() {
	busy = false;
}

Samurai::MessageHandler::~MessageHandler()
{
	for (MessageListener* one : listener) one->handler = nullptr;
}
	
void Samurai::MessageHandler::postMessage(size_t id, void* data, size_t arg1, size_t arg2)
{
	// We should perhaps allocate the new message from inside a static buffer.
	// In that case we could ignore freeing up memory, but it is always uncertain what the
	// worst case of queued messages would look like.
	auto msg = std::make_unique<Samurai::Message>(id, data, arg1, arg2);

	if (!busy)
		queue.push_front(std::move(msg));
	else
		busy_queue.push_front(std::move(msg));
}

void Samurai::MessageHandler::process() {
	busy = true;
	while (queue.size()) {
		handleMessage(queue.back().get());
		queue.pop_back();
	}

	/* Anything posted while processing is carried over to the next pass. */
	while (busy_queue.size()) {
		queue.push_front(std::move(busy_queue.back()));
		busy_queue.pop_back();
	}
	
	busy = false;
}

void Samurai::MessageHandler::handleMessage(Samurai::Message* msg) {
	for (Samurai::MessageListener* handler : listener)
		handler->EventMessage(msg);
}

void Samurai::MessageHandler::addMessageListener(Samurai::MessageListener* handler) {
//	QDBG("Samurai::MessageHandler::addMessageListener(): ptr=%p", handler);
	listener.push_back(handler);
}

void Samurai::MessageHandler::removeMessageListener(Samurai::MessageListener* handler) {
	/* Only the first match is removed, as before: a listener registers itself
	   once from its constructor. */
	const auto it = std::ranges::find(listener, handler);
	if (it != listener.end())
		listener.erase(it);
}

Samurai::MessageListener::MessageListener(Samurai::EventLoop& loop)
	: handler(&loop.messages())
{
	handler->addMessageListener(this);
}

Samurai::MessageListener::~MessageListener()
{
	if (handler) handler->removeMessageListener(this);
}



