/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/xml.h>
#include <samurai/util/string.h>

namespace {

constexpr bool is_space(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/*
 * A name may not begin with a digit, a hyphen or a full stop, and the colon is
 * excluded here because the caller has already split the prefix off at it.
 */
constexpr bool is_name_start(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       c == '_' || c >= 0x80;
}

constexpr bool is_name_char(unsigned char c)
{
	return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

}

/**
 * One parse of one document.
 *
 * The element stack is a member rather than the call stack: a recursive parser
 * makes the depth limit the only thing standing between a hostile document and
 * a stack overflow, which is not a limit worth relying on.
 */
/*
 * What the parser hands each piece of the document to.
 *
 * One parser, two things to do with what it finds: assemble a tree, or report it
 * as it goes. Anything the parser can decide - what nests, what matches, what is
 * within the limits - it decides itself, so a sink only has to take what it is
 * given and cannot make one kind of document parse differently from the other.
 */
class Samurai::IO::XmlSink
{
	public:
		virtual ~XmlSink() = default;

		using Attributes = XmlContentHandler::Attributes;

		virtual XmlError open(std::string&& prefix, std::string&& name,
			Attributes&& attributes) = 0;
		virtual XmlError close(std::string_view prefix, std::string_view name) = 0;
		virtual XmlError characters(std::string_view chunk) = 0;
};


/* Assembles the tree, which is what XmlDocument hands out. */
class Samurai::IO::XmlTreeSink final : public Samurai::IO::XmlSink
{
	public:
		XmlError open(std::string&& prefix, std::string&& name,
			Attributes&& attributes) override
		{
			std::unique_ptr<XmlElement> element(new XmlElement());
			element->prefix = std::move(prefix);
			element->name = std::move(name);
			element->attributes = std::move(attributes);

			XmlElement* borrowed = element.get();

			if (stack.empty()) document = std::move(element);
			else stack.back()->children.push_back(std::move(element));

			stack.push_back(borrowed);
			return XmlError::Ok;
		}

		XmlError close(std::string_view, std::string_view) override
		{
			stack.pop_back();
			return XmlError::Ok;
		}

		/*
		 * Joined onto whatever this element already had, so text either side of a
		 * child reads as one string - see the note on XmlElement::getText().
		 */
		XmlError characters(std::string_view chunk) override
		{
			stack.back()->text.append(chunk);
			return XmlError::Ok;
		}

		std::unique_ptr<XmlElement> release() { return std::move(document); }

	private:
		std::unique_ptr<XmlElement> document;
		/* Borrowed; the tree owns them. */
		std::vector<XmlElement*> stack;
};


/* Forwards to the caller's handler, keeping nothing. */
class Samurai::IO::XmlHandlerSink final : public Samurai::IO::XmlSink
{
	public:
		explicit XmlHandlerSink(XmlContentHandler& target) : handler(target) { }

		XmlError open(std::string&& prefix, std::string&& name,
			Attributes&& attributes) override
		{
			handler.startElement(prefix, name, attributes);
			return XmlError::Ok;
		}

		XmlError close(std::string_view prefix, std::string_view name) override
		{
			handler.endElement(prefix, name);
			return XmlError::Ok;
		}

		XmlError characters(std::string_view chunk) override
		{
			handler.characters(chunk);
			return XmlError::Ok;
		}

	private:
		XmlContentHandler& handler;
};


class Samurai::IO::XmlParser
{
	public:
		XmlParser(std::string_view input, const XmlDocument::Limits& bounds,
				XmlSink& target)
			: text(input), limits(bounds), sink(target) { }

		XmlError run();

		size_t offset() const { return pos; }

	private:
		bool done() const { return pos >= text.size(); }
		char at(size_t n) const { return text[n]; }
		bool looking_at(std::string_view token) const
		{ return text.compare(pos, token.size(), token) == 0; }

		void skip_space() { while (!done() && is_space((unsigned char) text[pos])) pos++; }

		XmlError parse_prolog();
		XmlError skip_misc(bool& skipped);
		XmlError parse_element();
		XmlError parse_end_tag();
		XmlError parse_name(std::string& prefix, std::string& name);
		XmlError parse_attributes(XmlSink::Attributes& attributes, bool& empty);
		XmlError parse_text();
		XmlError parse_cdata();
		XmlError append_reference(std::string& out);
		XmlError append_text(std::string& out, std::string_view chunk);

		/* Character data is bounded per element, and an element's data is
		   whatever was written inside it however many pieces that took - so the
		   running total belongs with the element rather than with the piece. */
		struct Open
		{
			std::string prefix;
			std::string name;
			size_t text_length = 0;
		};

		XmlError emit(std::string_view chunk);

		std::string_view text;
		const XmlDocument::Limits& limits;
		XmlSink& sink;
		size_t pos = 0;
		size_t elements = 0;
		bool had_root = false;

		std::vector<Open> stack;
};


/* One run of character data, counted against the element it belongs to. */
Samurai::IO::XmlError Samurai::IO::XmlParser::emit(std::string_view chunk)
{
	if (chunk.empty()) return XmlError::Ok;

	stack.back().text_length += chunk.size();
	return sink.characters(chunk);
}


/*
 * The cap is on an element's character data, not on the run being assembled: an
 * element's data is whatever was written inside it, however many runs that took,
 * so what has already been reported for this element counts towards it.
 *
 * Attribute values are assembled through here as well, where there is no element
 * open to count against - the value on its own is what the cap applies to.
 */
Samurai::IO::XmlError Samurai::IO::XmlParser::append_text(std::string& out,
                                                          std::string_view chunk)
{
	const size_t reported = stack.empty() ? 0 : stack.back().text_length;

	if (reported + out.size() + chunk.size() > limits.maxTextLength)
		return XmlError::SizeExceeded;

	out.append(chunk);
	return XmlError::Ok;
}


/*
 * The five predefined entities, and numeric character references.
 *
 * Anything else is an error rather than being copied through untouched: a
 * reference that is passed along is one that whatever consumes this string may
 * resolve instead, which is the same exposure the DOCTYPE refusal exists to
 * close.
 */
Samurai::IO::XmlError Samurai::IO::XmlParser::append_reference(std::string& out)
{
	const size_t semi = text.find(';', pos);
	if (semi == std::string_view::npos) return XmlError::Truncated;

	/* Bounded so a stray '&' cannot make this scan the whole document. */
	if (semi - pos > 32) return XmlError::Syntax;

	const std::string_view body = text.substr(pos + 1, semi - pos - 1);
	if (body.empty()) return XmlError::Syntax;

	if (body[0] == '#')
	{
		uint32_t value = 0;
		const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
		const std::string_view digits = body.substr(hex ? 2 : 1);
		if (digits.empty()) return XmlError::Syntax;

		for (const char digit : digits)
		{
			const unsigned char c = (unsigned char) digit;
			int d = -1;
			if (c >= '0' && c <= '9') d = c - '0';
			else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
			if (d < 0) return XmlError::Syntax;

			value = value * (hex ? 16 : 10) + (uint32_t) d;
			if (value > 0x10ffff) return XmlError::Syntax;
		}

		/* U+0000 is not a character, and append_utf8() refuses a surrogate. */
		if (value == 0) return XmlError::Syntax;
		if (out.size() + 4 > limits.maxTextLength) return XmlError::SizeExceeded;
		if (!Samurai::Util::append_utf8(value, out)) return XmlError::Syntax;

		pos = semi + 1;
		return XmlError::Ok;
	}

	std::string_view replacement;
	if      (body == "amp")  replacement = "&";
	else if (body == "lt")   replacement = "<";
	else if (body == "gt")   replacement = ">";
	else if (body == "quot") replacement = "\"";
	else if (body == "apos") replacement = "'";
	else return XmlError::Syntax;

	const XmlError status = append_text(out, replacement);
	if (status != XmlError::Ok) return status;

	pos = semi + 1;
	return XmlError::Ok;
}


Samurai::IO::XmlError Samurai::IO::XmlParser::parse_name(std::string& prefix, std::string& name)
{
	const size_t begin = pos;
	if (done() || !is_name_start((unsigned char) text[pos])) return XmlError::Syntax;

	while (!done() && (is_name_char((unsigned char) text[pos]) || text[pos] == ':')) pos++;

	std::string_view full = text.substr(begin, pos - begin);
	if (full.size() > limits.maxNameLength) return XmlError::SizeExceeded;

	/* Split at the first colon; see the namespace note on XmlDocument. */
	const size_t colon = full.find(':');
	if (colon == std::string_view::npos)
	{
		prefix.clear();
		name = std::string(full);
	}
	else
	{
		prefix = std::string(full.substr(0, colon));
		name = std::string(full.substr(colon + 1));
		if (name.empty() || prefix.empty()) return XmlError::Syntax;
		if (name.find(':') != std::string::npos) return XmlError::Syntax;
		if (!is_name_start((unsigned char) name[0])) return XmlError::Syntax;
	}

	return XmlError::Ok;
}


Samurai::IO::XmlError Samurai::IO::XmlParser::parse_attributes(
	XmlSink::Attributes& attributes, bool& empty)
{
	empty = false;

	for (;;)
	{
		skip_space();
		if (done()) return XmlError::Truncated;

		if (text[pos] == '>') { pos++; return XmlError::Ok; }

		if (looking_at("/>"))
		{
			pos += 2;
			empty = true;
			return XmlError::Ok;
		}

		std::string prefix;
		std::string name;
		const XmlError named = parse_name(prefix, name);
		if (named != XmlError::Ok) return named;

		/* An attribute's prefix is part of its name here: xmlns:foo is a
		   declaration this parser does not act on, and folding it to 'foo'
		   would collide with a real attribute of that name. */
		if (!prefix.empty()) name = prefix + ":" + name;

		skip_space();
		if (done()) return XmlError::Truncated;
		if (text[pos] != '=') return XmlError::Syntax;
		pos++;
		skip_space();
		if (done()) return XmlError::Truncated;

		const char quote = text[pos];
		if (quote != '"' && quote != '\'') return XmlError::Syntax;
		pos++;

		std::string value;
		for (;;)
		{
			if (done()) return XmlError::Truncated;

			if (text[pos] == quote) { pos++; break; }

			/* A raw '<' in an attribute value is never legal. */
			if (text[pos] == '<') return XmlError::Syntax;

			if (text[pos] == '&')
			{
				const XmlError expanded = append_reference(value);
				if (expanded != XmlError::Ok) return expanded;
				continue;
			}

			const size_t begin = pos;
			while (!done() && text[pos] != quote && text[pos] != '&' && text[pos] != '<') pos++;
			const XmlError added = append_text(value, text.substr(begin, pos - begin));
			if (added != XmlError::Ok) return added;
		}

		if (attributes.size() >= limits.maxAttributes) return XmlError::SizeExceeded;
		attributes.emplace_back(std::move(name), std::move(value));
	}
}


Samurai::IO::XmlError Samurai::IO::XmlParser::parse_cdata()
{
	/* Positioned on "<![CDATA[". */
	pos += 9;

	const size_t end = text.find("]]>", pos);
	if (end == std::string_view::npos) return XmlError::Truncated;

	std::string out;
	const XmlError added = append_text(out, text.substr(pos, end - pos));
	if (added != XmlError::Ok) return added;

	const XmlError sent = emit(out);
	if (sent != XmlError::Ok) return sent;

	pos = end + 3;
	return XmlError::Ok;
}


Samurai::IO::XmlError Samurai::IO::XmlParser::parse_text()
{
	std::string out;

	while (!done() && text[pos] != '<')
	{
		if (text[pos] == '&')
		{
			const XmlError expanded = append_reference(out);
			if (expanded != XmlError::Ok) return expanded;
			continue;
		}

		const size_t begin = pos;
		while (!done() && text[pos] != '<' && text[pos] != '&') pos++;

		const XmlError added = append_text(out, text.substr(begin, pos - begin));
		if (added != XmlError::Ok) return added;
	}

	return emit(out);
}


/**
 * Skip a comment or a processing instruction, and refuse a declaration.
 *
 * @param skipped set when something was consumed.
 */
Samurai::IO::XmlError Samurai::IO::XmlParser::skip_misc(bool& skipped)
{
	skipped = false;

	if (looking_at("<!--"))
	{
		pos += 4;
		const size_t end = text.find("-->", pos);
		if (end == std::string_view::npos) return XmlError::Truncated;

		/* "--" may not appear inside a comment. */
		if (text.substr(pos, end - pos).find("--") != std::string_view::npos)
			return XmlError::Syntax;

		pos = end + 3;
		skipped = true;
		return XmlError::Ok;
	}

	if (looking_at("<?"))
	{
		pos += 2;
		const size_t end = text.find("?>", pos);
		if (end == std::string_view::npos) return XmlError::Truncated;
		pos = end + 2;
		skipped = true;
		return XmlError::Ok;
	}

	/*
	 * A document type declaration is refused without being scanned. It is what
	 * carries entity declarations, so refusing it here is what closes external
	 * entity expansion and entity recursion outright rather than trying to
	 * survive them. Nothing UPnP sends needs one.
	 */
	if (looking_at("<!")) return XmlError::Unsupported;

	return XmlError::Ok;
}


Samurai::IO::XmlError Samurai::IO::XmlParser::parse_element()
{
	/* Positioned on '<' of a start tag. */
	pos++;

	if (++elements > limits.maxElements) return XmlError::SizeExceeded;
	if (stack.size() >= limits.maxDepth) return XmlError::DepthExceeded;

	/* A document has one root, and anything after it is not part of one. */
	if (stack.empty() && had_root) return XmlError::Syntax;

	std::string prefix;
	std::string name;
	const XmlError named = parse_name(prefix, name);
	if (named != XmlError::Ok) return named;

	bool empty = false;
	XmlSink::Attributes attributes;
	const XmlError attributed = parse_attributes(attributes, empty);
	if (attributed != XmlError::Ok) return attributed;

	if (stack.empty()) had_root = true;

	/*
	 * Pushed before the sink is told, and popped after: an empty element is a
	 * start and an end with nothing between, and a sink that is told about it
	 * has to see the same nesting either way.
	 */
	stack.push_back(Open{ prefix, name, 0 });

	const XmlError opened = sink.open(std::move(prefix), std::move(name),
		std::move(attributes));
	if (opened != XmlError::Ok) return opened;

	if (empty)
	{
		const Open closing = stack.back();
		const XmlError closed = sink.close(closing.prefix, closing.name);
		if (closed != XmlError::Ok) return closed;
		stack.pop_back();
	}

	return XmlError::Ok;
}


Samurai::IO::XmlError Samurai::IO::XmlParser::parse_end_tag()
{
	/* Positioned on "</". */
	pos += 2;

	std::string prefix;
	std::string name;
	const XmlError named = parse_name(prefix, name);
	if (named != XmlError::Ok) return named;

	skip_space();
	if (done()) return XmlError::Truncated;
	if (text[pos] != '>') return XmlError::Syntax;
	pos++;

	if (stack.empty()) return XmlError::Mismatch;
	if (stack.back().name != name) return XmlError::Mismatch;

	const XmlError closed = sink.close(prefix, name);
	if (closed != XmlError::Ok) return closed;

	stack.pop_back();
	return XmlError::Ok;
}


Samurai::IO::XmlError Samurai::IO::XmlParser::parse_prolog()
{
	/* A byte order mark is legitimate and says only that this is UTF-8. */
	if (text.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;

	return XmlError::Ok;
}


Samurai::IO::XmlError Samurai::IO::XmlParser::run()
{
	if (text.size() > limits.maxDocumentSize) return XmlError::SizeExceeded;

	const XmlError prolog = parse_prolog();
	if (prolog != XmlError::Ok) return prolog;

	while (!done())
	{
		if (text[pos] != '<')
		{
			/* Character data outside the root is whitespace or nothing. */
			if (stack.empty())
			{
				if (!is_space((unsigned char) text[pos])) return XmlError::Syntax;
				pos++;
				continue;
			}

			const XmlError texted = parse_text();
			if (texted != XmlError::Ok) return texted;
			continue;
		}

		if (looking_at("<![CDATA["))
		{
			if (stack.empty()) return XmlError::Syntax;
			const XmlError data = parse_cdata();
			if (data != XmlError::Ok) return data;
			continue;
		}

		bool skipped = false;
		const XmlError misc = skip_misc(skipped);
		if (misc != XmlError::Ok) return misc;
		if (skipped) continue;

		if (looking_at("</"))
		{
			const XmlError ended = parse_end_tag();
			if (ended != XmlError::Ok) return ended;
			continue;
		}

		/* A lone '<' with nothing after it is a truncated tag, not a syntax
		   error: the distinction is what tells a short read from bad markup. */
		if (pos + 1 >= text.size()) return XmlError::Truncated;

		const XmlError started = parse_element();
		if (started != XmlError::Ok) return started;
	}

	if (!stack.empty()) return XmlError::Truncated;
	if (!had_root) return XmlError::Truncated;

	return XmlError::Ok;
}


const char* Samurai::IO::toString(Samurai::IO::XmlError error)
{
	switch (error)
	{
		case XmlError::Ok:            return "ok";
		case XmlError::Truncated:     return "document ended early";
		case XmlError::Syntax:        return "malformed markup";
		case XmlError::Mismatch:      return "mismatched end tag";
		case XmlError::DepthExceeded: return "nested too deeply";
		case XmlError::SizeExceeded:  return "document too large";
		case XmlError::Unsupported:   return "document type declarations are refused";
	}
	return "unknown error";
}


Samurai::IO::XmlElement::~XmlElement()
{
}


std::optional<std::string>
Samurai::IO::XmlElement::getAttribute(std::string_view attribute) const
{
	for (const auto& [key, value] : attributes)
		if (key == attribute) return value;

	return std::nullopt;
}


const Samurai::IO::XmlElement*
Samurai::IO::XmlElement::findChild(std::string_view child) const
{
	for (const std::unique_ptr<XmlElement>& candidate : children)
		if (candidate->name == child) return candidate.get();

	return nullptr;
}


std::vector<const Samurai::IO::XmlElement*>
Samurai::IO::XmlElement::findChildren(std::string_view child) const
{
	std::vector<const XmlElement*> found;
	for (const std::unique_ptr<XmlElement>& candidate : children)
		if (candidate->name == child) found.push_back(candidate.get());

	return found;
}


/*
 * Breadth first, and iterative: a device description nests a WAN connection
 * device several levels down, and the depth is whatever the vendor chose.
 */
const Samurai::IO::XmlElement*
Samurai::IO::XmlElement::findDescendant(std::string_view descendant) const
{
	std::vector<const XmlElement*> level;
	level.push_back(this);

	while (!level.empty())
	{
		std::vector<const XmlElement*> next;
		for (const XmlElement* element : level)
		{
			for (const std::unique_ptr<XmlElement>& candidate : element->children)
			{
				if (candidate->name == descendant) return candidate.get();
				next.push_back(candidate.get());
			}
		}
		level = std::move(next);
	}

	return nullptr;
}


std::string Samurai::IO::XmlElement::getChildText(std::string_view child) const
{
	const XmlElement* found = findChild(child);
	if (!found) return std::string();

	return std::string(Samurai::Util::trim(found->text));
}


Samurai::IO::XmlDocument::XmlDocument()
{
}


Samurai::IO::XmlDocument::~XmlDocument()
{
}


Samurai::IO::XmlDocument::XmlDocument(XmlDocument&&) noexcept = default;


Samurai::IO::XmlDocument& Samurai::IO::XmlDocument::operator=(XmlDocument&&) noexcept = default;


Samurai::IO::XmlError Samurai::IO::XmlDocument::parse(std::string_view text)
{
	const Limits limits;
	return parse(text, limits);
}


Samurai::IO::XmlError Samurai::IO::XmlDocument::parse(std::string_view text,
                                                     const Limits& limits)
{
	root.reset();
	error = XmlError::Ok;
	error_offset = 0;

	XmlTreeSink sink;
	XmlParser parser(text, limits, sink);
	error = parser.run();
	error_offset = parser.offset();

	/* Nothing is handed out from a document that failed: a half-built tree
	   invites a caller to read whatever happened to parse before the error. */
	if (error != XmlError::Ok) return error;

	root = sink.release();
	return error;
}


Samurai::IO::XmlError Samurai::IO::XmlReader::parse(std::string_view text,
	XmlContentHandler& handler)
{
	const Limits limits;
	return parse(text, handler, limits);
}


Samurai::IO::XmlError Samurai::IO::XmlReader::parse(std::string_view text,
	XmlContentHandler& handler, const Limits& limits, size_t* errorOffset)
{
	XmlHandlerSink sink(handler);
	XmlParser parser(text, limits, sink);

	handler.startDocument();

	const XmlError error = parser.run();
	if (errorOffset) *errorOffset = parser.offset();

	/*
	 * Only a document that parsed gets an end. A handler that has been told the
	 * document ended has been told the events it received were all of them, and
	 * for a failed parse they were not.
	 */
	if (error == XmlError::Ok) handler.endDocument();

	return error;
}
