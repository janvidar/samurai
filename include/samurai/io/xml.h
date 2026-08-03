/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_XML_H
#define HAVE_SAMURAI_XML_H

#include <stddef.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Samurai {
namespace IO {

/**
 * Why a document was refused.
 *
 * Returned rather than reported through a std::error_code out-parameter, as
 * Codec::Status and IO::ReadResult are: none of these is an errno value, and
 * wrapping them would need an error category that says nothing the enumerator
 * does not.
 */
enum class XmlError
{
	Ok,
	Truncated,      /**< the document ended inside a construct */
	Syntax,         /**< malformed markup, or an entity that cannot be resolved */
	Mismatch,       /**< an end tag naming something other than the open element */
	DepthExceeded,
	SizeExceeded,   /**< a document, element, text or attribute limit */
	Unsupported     /**< a document type or entity declaration; refused, not parsed */
};

/** What XmlError means, for a diagnostic. */
const char* toString(XmlError error);

class XmlDocument;
/* Builds the tree. Defined in the implementation; named here only so that
   XmlElement can grant it the access no one else has. */
class XmlParser;

/**
 * One element of a parsed document.
 *
 * Owned by the XmlDocument it was parsed from, and valid for as long as that
 * document is.
 */
class XmlElement final
{
	public:
		~XmlElement();

		/* Owns its children, so a copy would release them twice. */
		XmlElement(const XmlElement&) = delete;
		XmlElement& operator=(const XmlElement&) = delete;

		/**
		 * The name with any namespace prefix removed, so 's:Body' reports
		 * 'Body'. See the note on namespaces in the class comment of
		 * XmlDocument.
		 */
		const std::string& getName() const { return name; }

		/** The namespace prefix as it was written, empty when there was none. */
		const std::string& getPrefix() const { return prefix; }

		/**
		 * The character data directly inside this element, as written.
		 *
		 * Data separated by a child element is joined, so <a>x<b/>y</a> reports
		 * "xy". Nothing UPnP needs distinguishes the two halves, and a caller
		 * that did would want a node list rather than this.
		 */
		const std::string& getText() const { return text; }

		std::optional<std::string> getAttribute(std::string_view attribute) const;

		const std::vector<std::pair<std::string, std::string>>& getAttributes() const
		{ return attributes; }

		const std::vector<std::unique_ptr<XmlElement>>& getChildren() const
		{ return children; }

		/** The first direct child with this name, or null if there is none. */
		const XmlElement* findChild(std::string_view child) const;

		/** Every direct child with this name, in document order. */
		std::vector<const XmlElement*> findChildren(std::string_view child) const;

		/** The first descendant with this name, breadth first, or null. */
		const XmlElement* findDescendant(std::string_view descendant) const;

		/**
		 * The text of the first direct child with this name, with leading and
		 * trailing whitespace removed, or empty when there is no such child.
		 *
		 * This is what nearly every caller wants: a pretty-printed document
		 * indents its leaf values, so the raw text of <controlURL> is usually
		 * surrounded by a newline and a run of tabs.
		 */
		std::string getChildText(std::string_view child) const;

	private:
		XmlElement() = default;

		std::string name;
		std::string prefix;
		std::string text;
		std::vector<std::pair<std::string, std::string>> attributes;
		std::vector<std::unique_ptr<XmlElement>> children;

	friend class XmlDocument;
	friend class XmlParser;
};

/**
 * A parsed XML document, held as a tree.
 *
 * Scope is deliberately narrow: enough of XML to read a UPnP device
 * description and a SOAP response, and no more. What is missing is missing on
 * purpose.
 *
 * NAMESPACES: a prefix is split off at the first colon and matching is on the
 * local name alone. An xmlns attribute is an ordinary attribute; nothing is
 * bound or resolved. So findChild("Body") matches <s:Body>, <SOAP-ENV:Body> and
 * <Body> alike, which is what makes this usable against SOAP - vendors pick
 * their own prefixes, so matching the written name matches nothing reliably.
 * Resolving namespaces properly would several times the size of this parser to
 * defend against prefix aliasing by a device whose control URL is already being
 * trusted.
 *
 * SECURITY: this parses bytes from a device on the local network that has not
 * been authenticated. A document type declaration is refused outright rather
 * than skipped, as is any entity declaration, which is what closes external
 * entity expansion and the entity-recursion denial of service instead of
 * mitigating them. An entity reference naming anything but the five predefined
 * ones is an error and is never passed through: passing it through is how a
 * parser ends up being the one that resolves it later. Every dimension of the
 * document is bounded, and the element stack is explicit rather than the call
 * stack, so depth is a limit rather than a crash.
 *
 * ENCODING: the input is assumed to be UTF-8, which UPnP requires. A leading
 * byte order mark is skipped. An XML declaration naming another encoding is
 * ignored rather than honoured.
 */
class XmlDocument final
{
	public:
		/**
		 * Bounds on what will be parsed. These are defences rather than tuning
		 * knobs: every one of them is what stops a hostile document from
		 * costing more than the answer is worth.
		 */
		struct Limits
		{
			size_t maxDocumentSize = 256 * 1024;
			size_t maxDepth        = 32;
			size_t maxElements     = 8192;
			size_t maxAttributes   = 32;        /**< per element */
			size_t maxTextLength   = 64 * 1024; /**< per element */
			size_t maxNameLength   = 256;
		};

		XmlDocument();
		~XmlDocument();

		XmlDocument(XmlDocument&&) noexcept;
		XmlDocument& operator=(XmlDocument&&) noexcept;

		/* Owns a tree of unique_ptr, so there is no copy to make cheaply. */
		XmlDocument(const XmlDocument&) = delete;
		XmlDocument& operator=(const XmlDocument&) = delete;

		XmlError parse(std::string_view text);
		XmlError parse(std::string_view text, const Limits& limits);

		/** The document element, or null unless parse() returned Ok. */
		const XmlElement* getRoot() const { return root.get(); }

		XmlError getError() const { return error; }

		/** Where the parse stopped. Only meaningful once it has failed. */
		size_t getErrorOffset() const { return error_offset; }

	private:
		std::unique_ptr<XmlElement> root;
		XmlError error = XmlError::Ok;
		size_t error_offset = 0;
};

}
}

#endif // HAVE_SAMURAI_XML_H
