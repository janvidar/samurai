/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#ifndef HAVE_SAMURAI_JSON_H
#define HAVE_SAMURAI_JSON_H

#include <stddef.h>
#include <stdint.h>
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
 * Returned rather than reported through a std::error_code out-parameter, for
 * the same reason XmlError is: none of these is an errno value.
 */
enum class JsonError
{
	Ok,
	Truncated,      /**< the document ended inside a value */
	Syntax,         /**< malformed text, or a bad escape */
	DepthExceeded,
	SizeExceeded,   /**< a document, string, member or element limit */
	TrailingData,   /**< a complete value followed by something other than space */
	NumberRange     /**< a number that cannot be represented */
};

/** What JsonError means, for a diagnostic. */
const char* toString(JsonError error);

class JsonParser;

/**
 * One value in a parsed document.
 *
 * Owned by the JsonDocument it was parsed from, and valid for as long as that
 * document is.
 *
 * The accessors do not throw and do not abort on a type mismatch: asking a
 * string for its number gives the default. A document arriving over a network
 * has whatever shape the sender chose, and a reader written against a schema
 * it half matches should get an unsurprising value rather than a crash. Use
 * getType() or the is* predicates where the difference matters.
 */
class JsonValue final
{
	public:
		enum class Type { Null, Boolean, Number, String, Array, Object };

		~JsonValue();

		/* Owns its children, so a copy would release them twice. */
		JsonValue(const JsonValue&) = delete;
		JsonValue& operator=(const JsonValue&) = delete;

		Type getType() const { return type; }

		bool isNull()    const { return type == Type::Null; }
		bool isBoolean() const { return type == Type::Boolean; }
		bool isNumber()  const { return type == Type::Number; }
		bool isString()  const { return type == Type::String; }
		bool isArray()   const { return type == Type::Array; }
		bool isObject()  const { return type == Type::Object; }

		bool getBoolean(bool fallback = false) const;
		double getNumber(double fallback = 0.0) const;

		/**
		 * The value as a signed integer.
		 *
		 * A number with a fractional part, or one outside the range, gives the
		 * fallback rather than a truncation: a port of 1511.9 is a protocol
		 * error at the sender, and rounding it silently hides that.
		 */
		int64_t getInteger(int64_t fallback = 0) const;

		/** Empty for anything that is not a string. */
		const std::string& getString() const;
		std::string getString(std::string_view fallback) const;

		/** Elements of an array, or members of an object, in document order. */
		size_t size() const;

		/** The element at an index, or null when out of range or not an array. */
		const JsonValue* at(size_t index) const;

		/** The value of a member, or null when absent or not an object. */
		const JsonValue* find(std::string_view member) const;

		/** Members in document order, for a caller that wants to walk them. */
		const std::vector<std::pair<std::string, std::unique_ptr<JsonValue>>>&
		getMembers() const { return members; }

		const std::vector<std::unique_ptr<JsonValue>>& getElements() const
		{ return elements; }

		/*
		 * Typed lookups, which is what reading a known schema actually looks
		 * like. Each gives the fallback when the member is absent or is not of
		 * that type, so a reader is a list of these rather than a list of null
		 * checks.
		 */
		bool findBoolean(std::string_view member, bool fallback = false) const;
		int64_t findInteger(std::string_view member, int64_t fallback = 0) const;
		double findNumber(std::string_view member, double fallback = 0.0) const;
		std::string findString(std::string_view member,
		                       std::string_view fallback = {}) const;

		/** Distinguishes an absent member from one whose value is a default. */
		std::optional<int64_t> findOptionalInteger(std::string_view member) const;
		std::optional<std::string> findOptionalString(std::string_view member) const;

	private:
		JsonValue() = default;

		Type type = Type::Null;
		bool boolean = false;
		double number = 0.0;
		/** Whether the number was written without a fraction or exponent. */
		bool integral = false;
		int64_t integer = 0;
		std::string string;

		std::vector<std::unique_ptr<JsonValue>> elements;
		std::vector<std::pair<std::string, std::unique_ptr<JsonValue>>> members;

	friend class JsonDocument;
	friend class JsonParser;
};

/**
 * A parsed JSON document, held as a tree.
 *
 * RFC 8259, and no more than that. Comments, trailing commas, single quotes,
 * unquoted member names, NaN and Infinity are all refused; every one of them is
 * an extension some parser accepts, and accepting them here would mean a
 * document this reads and a conforming one disagree.
 *
 * SECURITY: this parses bytes from a network peer. Every dimension is bounded,
 * and nesting is held on an explicit stack rather than the call stack, so a
 * document nested a million deep is DepthExceeded rather than a smashed stack.
 * A duplicate member is kept rather than merged, and find() returns the first:
 * discarding one of two spellings of the same name is how two readers of the
 * same document come to disagree about what it said.
 *
 * ENCODING: input is UTF-8. A leading byte order mark is skipped. \\u escapes
 * are decoded to UTF-8, including surrogate pairs; an unpaired surrogate is an
 * error rather than being passed through, since passing it through produces a
 * string that is not valid UTF-8 and fails somewhere further away.
 */
class JsonDocument final
{
	public:
		/**
		 * Bounds on what will be parsed. These are defences rather than tuning
		 * knobs.
		 */
		struct Limits
		{
			size_t maxDocumentSize = 256 * 1024;
			size_t maxDepth        = 32;
			size_t maxValues       = 8192;
			size_t maxStringLength = 64 * 1024;
			size_t maxMembers      = 256;  /**< per object */
			size_t maxElements     = 4096; /**< per array */
		};

		JsonDocument();
		~JsonDocument();

		JsonDocument(JsonDocument&&) noexcept;
		JsonDocument& operator=(JsonDocument&&) noexcept;

		/* Owns a tree of unique_ptr, so there is no copy to make cheaply. */
		JsonDocument(const JsonDocument&) = delete;
		JsonDocument& operator=(const JsonDocument&) = delete;

		JsonError parse(std::string_view text);
		JsonError parse(std::string_view text, const Limits& limits);

		/** The root value, or null unless parse() returned Ok. */
		const JsonValue* getRoot() const { return root.get(); }

		JsonError getError() const { return error; }

		/** Where the parse stopped. Only meaningful once it has failed. */
		size_t getErrorOffset() const { return error_offset; }

	private:
		std::unique_ptr<JsonValue> root;
		JsonError error = JsonError::Ok;
		size_t error_offset = 0;
};


/**
 * Builds a JSON document.
 *
 * Compact output, no pretty printing: this exists to produce request bodies,
 * which nothing reads by eye.
 *
 * Misuse is caught rather than emitted. Ending an object that was not begun, or
 * writing a value where a member name belongs, sets a failure that str()
 * reports, so a mistake surfaces as an empty result at the call site instead of
 * as malformed JSON at the far end.
 */
class JsonWriter final
{
	public:
		JsonWriter();
		~JsonWriter();

		JsonWriter& beginObject();
		JsonWriter& endObject();
		JsonWriter& beginArray();
		JsonWriter& endArray();

		/** The name of the next member. Only inside an object. */
		JsonWriter& member(std::string_view name);

		JsonWriter& value(std::string_view text);
		JsonWriter& value(const char* text);
		JsonWriter& value(int64_t number);
		JsonWriter& value(double number);
		JsonWriter& value(bool boolean);
		JsonWriter& valueNull();

		/* Name and value together, which is what most calls are. */
		JsonWriter& member(std::string_view name, std::string_view text);
		JsonWriter& member(std::string_view name, const char* text);
		JsonWriter& member(std::string_view name, int64_t number);
		JsonWriter& member(std::string_view name, bool boolean);

		/** Whether every call so far was well formed and the value is complete. */
		bool isComplete() const;

		/** The document, or empty if anything was misused or is unfinished. */
		std::string str() const;

	private:
		void separate();

		enum class Frame { Array, Object };

		std::string out;
		std::vector<Frame> stack;
		/** Whether the current frame has had an entry, so a comma is needed. */
		std::vector<bool> populated;
		/** Set inside an object between member() and its value. */
		bool expecting_value = false;
		bool failed = false;
		bool have_root = false;
};

/** Escape a string as a JSON string literal, quotes included. */
std::string jsonEscape(std::string_view text);

}
}

#endif // HAVE_SAMURAI_JSON_H
