/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/json.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits>

namespace Samurai {
namespace IO {

const char* toString(JsonError error)
{
	switch (error)
	{
		case JsonError::Ok:            return "ok";
		case JsonError::Truncated:     return "document ended inside a value";
		case JsonError::Syntax:        return "malformed JSON";
		case JsonError::DepthExceeded: return "nested too deeply";
		case JsonError::SizeExceeded:  return "document exceeds a size limit";
		case JsonError::TrailingData:  return "data after the end of the value";
		case JsonError::NumberRange:   return "number out of range";
	}
	return "unknown error";
}

/* --- JsonValue ---------------------------------------------------------- */

JsonValue::~JsonValue() = default;

static const std::string g_empty_string;

bool JsonValue::getBoolean(bool fallback) const
{
	return type == Type::Boolean ? boolean : fallback;
}

double JsonValue::getNumber(double fallback) const
{
	return type == Type::Number ? number : fallback;
}

int64_t JsonValue::getInteger(int64_t fallback) const
{
	/* Only a number written without a fraction or exponent counts. 1511.9 is a
	   mistake at the sender, and truncating it here hides that from everyone. */
	if (type != Type::Number || !integral)
		return fallback;
	return integer;
}

const std::string& JsonValue::getString() const
{
	return type == Type::String ? string : g_empty_string;
}

std::string JsonValue::getString(std::string_view fallback) const
{
	return type == Type::String ? string : std::string(fallback);
}

size_t JsonValue::size() const
{
	if (type == Type::Array)  return elements.size();
	if (type == Type::Object) return members.size();
	return 0;
}

const JsonValue* JsonValue::at(size_t index) const
{
	if (type != Type::Array || index >= elements.size())
		return nullptr;
	return elements[index].get();
}

const JsonValue* JsonValue::find(std::string_view member) const
{
	if (type != Type::Object)
		return nullptr;
	for (const auto& entry : members)
	{
		if (entry.first == member)
			return entry.second.get();
	}
	return nullptr;
}

bool JsonValue::findBoolean(std::string_view member, bool fallback) const
{
	const JsonValue* value = find(member);
	return value ? value->getBoolean(fallback) : fallback;
}

int64_t JsonValue::findInteger(std::string_view member, int64_t fallback) const
{
	const JsonValue* value = find(member);
	return value ? value->getInteger(fallback) : fallback;
}

double JsonValue::findNumber(std::string_view member, double fallback) const
{
	const JsonValue* value = find(member);
	return value ? value->getNumber(fallback) : fallback;
}

std::string JsonValue::findString(std::string_view member,
                                  std::string_view fallback) const
{
	const JsonValue* value = find(member);
	return value ? value->getString(fallback) : std::string(fallback);
}

std::optional<int64_t> JsonValue::findOptionalInteger(std::string_view member) const
{
	const JsonValue* value = find(member);
	if (!value || value->type != Type::Number || !value->integral)
		return std::nullopt;
	return value->integer;
}

std::optional<std::string> JsonValue::findOptionalString(std::string_view member) const
{
	const JsonValue* value = find(member);
	if (!value || value->type != Type::String)
		return std::nullopt;
	return value->string;
}

/* --- parser -------------------------------------------------------------- */

/**
 * A recursive-descent parser with the recursion taken out.
 *
 * Nesting lives on 'stack' rather than on the call stack, which is what turns
 * a document nested a million deep from a crash into DepthExceeded. The shape
 * is: read one value; if it is a container, push it and go round again looking
 * for the next thing that container wants.
 */
class JsonParser final
{
	public:
		JsonParser(std::string_view text, const JsonDocument::Limits& limits)
			: in(text), limits(limits) { }

		JsonError parse(std::unique_ptr<JsonValue>& out, size_t& errorOffset);

	private:
		/* What a container is waiting for, so the loop knows what may come next. */
		struct Frame
		{
			JsonValue* value = nullptr;
			bool object = false;
		};

		bool eof() const { return pos >= in.size(); }
		char peek() const { return in[pos]; }

		void skipSpace();
		bool literal(std::string_view word);

		JsonError readValue(std::unique_ptr<JsonValue>& out, bool& isContainer);
		JsonError readString(std::string& out);
		JsonError readNumber(JsonValue& out);
		bool readHex4(uint32_t& out);
		static void appendUtf8(std::string& out, uint32_t cp);

		JsonError fail(JsonError why) { error_offset = pos; return why; }

		std::string_view in;
		JsonDocument::Limits limits;
		size_t pos = 0;
		size_t values = 0;
		size_t error_offset = 0;
};

void JsonParser::skipSpace()
{
	/* RFC 8259 whitespace, and only that: a vertical tab or a form feed
	   between tokens is not JSON however forgiving other parsers are. */
	while (pos < in.size())
	{
		const char c = in[pos];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			pos++;
		else
			break;
	}
}

bool JsonParser::literal(std::string_view word)
{
	if (in.size() - pos < word.size())
		return false;
	if (in.compare(pos, word.size(), word) != 0)
		return false;
	pos += word.size();
	return true;
}

bool JsonParser::readHex4(uint32_t& out)
{
	if (in.size() - pos < 4)
		return false;
	uint32_t value = 0;
	for (int i = 0; i < 4; i++)
	{
		const char c = in[pos + i];
		value <<= 4;
		if (c >= '0' && c <= '9')      value |= static_cast<uint32_t>(c - '0');
		else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
		else return false;
	}
	pos += 4;
	out = value;
	return true;
}

void JsonParser::appendUtf8(std::string& out, uint32_t cp)
{
	if (cp < 0x80)
	{
		out.push_back(static_cast<char>(cp));
	}
	else if (cp < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else if (cp < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

JsonError JsonParser::readString(std::string& out)
{
	if (eof() || peek() != '"')
		return fail(JsonError::Syntax);
	pos++;

	out.clear();
	for (;;)
	{
		if (eof())
			return fail(JsonError::Truncated);

		const unsigned char c = static_cast<unsigned char>(in[pos]);

		if (c == '"')
		{
			pos++;
			return JsonError::Ok;
		}

		if (c < 0x20)
		{
			/* A raw control character in a string is not JSON. Accepting it
			   would let a newline inside a string end a line-delimited
			   framing that the reader and the writer no longer agree on. */
			return fail(JsonError::Syntax);
		}

		if (out.size() >= limits.maxStringLength)
			return fail(JsonError::SizeExceeded);

		if (c != '\\')
		{
			out.push_back(static_cast<char>(c));
			pos++;
			continue;
		}

		pos++;
		if (eof())
			return fail(JsonError::Truncated);

		const char esc = in[pos++];
		switch (esc)
		{
			case '"':  out.push_back('"');  break;
			case '\\': out.push_back('\\'); break;
			case '/':  out.push_back('/');  break;
			case 'b':  out.push_back('\b'); break;
			case 'f':  out.push_back('\f'); break;
			case 'n':  out.push_back('\n'); break;
			case 'r':  out.push_back('\r'); break;
			case 't':  out.push_back('\t'); break;
			case 'u':
			{
				uint32_t cp = 0;
				if (!readHex4(cp))
					return fail(JsonError::Syntax);

				if (cp >= 0xD800 && cp <= 0xDBFF)
				{
					/* A high surrogate must be followed by its low half. An
					   unpaired one is refused rather than passed through:
					   passing it through yields a string that is not valid
					   UTF-8, and the failure then surfaces somewhere with no
					   idea where it came from. */
					if (in.size() - pos < 2 || in[pos] != '\\' || in[pos + 1] != 'u')
						return fail(JsonError::Syntax);
					pos += 2;
					uint32_t low = 0;
					if (!readHex4(low))
						return fail(JsonError::Syntax);
					if (low < 0xDC00 || low > 0xDFFF)
						return fail(JsonError::Syntax);
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
				}
				else if (cp >= 0xDC00 && cp <= 0xDFFF)
				{
					return fail(JsonError::Syntax); /* a lone low surrogate */
				}

				if (out.size() + 4 > limits.maxStringLength)
					return fail(JsonError::SizeExceeded);
				appendUtf8(out, cp);
				break;
			}
			default:
				return fail(JsonError::Syntax);
		}
	}
}

JsonError JsonParser::readNumber(JsonValue& out)
{
	const size_t start = pos;

	if (!eof() && peek() == '-')
		pos++;

	/* An integer part is required, and a leading zero may not be followed by
	   another digit: 007 is not JSON. */
	if (eof())
		return fail(JsonError::Truncated);
	if (peek() == '0')
	{
		pos++;
	}
	else if (peek() >= '1' && peek() <= '9')
	{
		while (!eof() && peek() >= '0' && peek() <= '9')
			pos++;
	}
	else
	{
		return fail(JsonError::Syntax);
	}

	bool integral = true;

	if (!eof() && peek() == '.')
	{
		integral = false;
		pos++;
		if (eof() || peek() < '0' || peek() > '9')
			return fail(JsonError::Syntax);
		while (!eof() && peek() >= '0' && peek() <= '9')
			pos++;
	}

	if (!eof() && (peek() == 'e' || peek() == 'E'))
	{
		integral = false;
		pos++;
		if (!eof() && (peek() == '+' || peek() == '-'))
			pos++;
		if (eof() || peek() < '0' || peek() > '9')
			return fail(JsonError::Syntax);
		while (!eof() && peek() >= '0' && peek() <= '9')
			pos++;
	}

	const std::string text(in.substr(start, pos - start));

	errno = 0;
	char* end = nullptr;
	const double d = strtod(text.c_str(), &end);
	if (end != text.c_str() + text.size())
		return fail(JsonError::Syntax);
	if (errno == ERANGE && (d > 1.0 || d < -1.0))
		return fail(JsonError::NumberRange);
	if (!isfinite(d))
		return fail(JsonError::NumberRange);

	out.type = JsonValue::Type::Number;
	out.number = d;
	out.integral = false;

	if (integral)
	{
		errno = 0;
		char* iend = nullptr;
		const long long ll = strtoll(text.c_str(), &iend, 10);
		if (errno == 0 && iend == text.c_str() + text.size())
		{
			out.integral = true;
			out.integer = static_cast<int64_t>(ll);
		}
		/* Written as an integer but too large for one: it keeps its double
		   value, and getInteger() gives the caller's fallback rather than a
		   wrapped number. */
	}
	return JsonError::Ok;
}

JsonError JsonParser::readValue(std::unique_ptr<JsonValue>& out, bool& isContainer)
{
	isContainer = false;

	if (++values > limits.maxValues)
		return fail(JsonError::SizeExceeded);

	if (eof())
		return fail(JsonError::Truncated);

	out.reset(new JsonValue());

	switch (peek())
	{
		case '{':
			pos++;
			out->type = JsonValue::Type::Object;
			isContainer = true;
			return JsonError::Ok;

		case '[':
			pos++;
			out->type = JsonValue::Type::Array;
			isContainer = true;
			return JsonError::Ok;

		case '"':
		{
			out->type = JsonValue::Type::String;
			return readString(out->string);
		}

		case 't':
			if (!literal("true"))
				return fail(JsonError::Syntax);
			out->type = JsonValue::Type::Boolean;
			out->boolean = true;
			return JsonError::Ok;

		case 'f':
			if (!literal("false"))
				return fail(JsonError::Syntax);
			out->type = JsonValue::Type::Boolean;
			out->boolean = false;
			return JsonError::Ok;

		case 'n':
			if (!literal("null"))
				return fail(JsonError::Syntax);
			out->type = JsonValue::Type::Null;
			return JsonError::Ok;

		default:
			return readNumber(*out);
	}
}

JsonError JsonParser::parse(std::unique_ptr<JsonValue>& out, size_t& errorOffset)
{
	if (in.size() > limits.maxDocumentSize)
	{
		errorOffset = limits.maxDocumentSize;
		return JsonError::SizeExceeded;
	}

	/* A byte order mark is not JSON, but it is what a Windows text editor
	   leaves at the front of a file, so it is skipped rather than refused. */
	if (in.size() >= 3 &&
	    static_cast<unsigned char>(in[0]) == 0xEF &&
	    static_cast<unsigned char>(in[1]) == 0xBB &&
	    static_cast<unsigned char>(in[2]) == 0xBF)
	{
		pos = 3;
	}

	skipSpace();

	std::unique_ptr<JsonValue> root;
	bool isContainer = false;
	JsonError err = readValue(root, isContainer);
	if (err != JsonError::Ok)
	{
		errorOffset = error_offset;
		return err;
	}

	std::vector<Frame> stack;
	if (isContainer)
		stack.push_back(Frame{ root.get(), root->type == JsonValue::Type::Object });

	while (!stack.empty())
	{
		if (stack.size() > limits.maxDepth)
		{
			errorOffset = pos;
			return JsonError::DepthExceeded;
		}

		Frame& frame = stack.back();
		skipSpace();
		if (eof())
		{
			errorOffset = pos;
			return JsonError::Truncated;
		}

		const bool empty = frame.object ? frame.value->members.empty()
		                                : frame.value->elements.empty();

		/* The close of the container we are in. */
		const char closer = frame.object ? '}' : ']';
		if (peek() == closer)
		{
			pos++;
			stack.pop_back();
			continue;
		}

		if (!empty)
		{
			if (peek() != ',')
			{
				errorOffset = pos;
				return JsonError::Syntax;
			}
			pos++;
			skipSpace();
			/* A trailing comma is accepted by several parsers and is not JSON;
			   refusing it here keeps this and a conforming reader agreeing. */
			if (!eof() && peek() == closer)
			{
				errorOffset = pos;
				return JsonError::Syntax;
			}
		}

		std::string name;
		if (frame.object)
		{
			if (frame.value->members.size() >= limits.maxMembers)
			{
				errorOffset = pos;
				return JsonError::SizeExceeded;
			}
			err = readString(name);
			if (err != JsonError::Ok)
			{
				errorOffset = error_offset;
				return err;
			}
			skipSpace();
			if (eof() || peek() != ':')
			{
				errorOffset = pos;
				return eof() ? JsonError::Truncated : JsonError::Syntax;
			}
			pos++;
			skipSpace();
		}
		else if (frame.value->elements.size() >= limits.maxElements)
		{
			errorOffset = pos;
			return JsonError::SizeExceeded;
		}

		std::unique_ptr<JsonValue> child;
		bool childIsContainer = false;
		err = readValue(child, childIsContainer);
		if (err != JsonError::Ok)
		{
			errorOffset = error_offset;
			return err;
		}

		JsonValue* raw = child.get();
		if (frame.object)
			frame.value->members.emplace_back(std::move(name), std::move(child));
		else
			frame.value->elements.push_back(std::move(child));

		if (childIsContainer)
			stack.push_back(Frame{ raw, raw->type == JsonValue::Type::Object });
	}

	skipSpace();
	if (!eof())
	{
		errorOffset = pos;
		return JsonError::TrailingData;
	}

	out = std::move(root);
	errorOffset = pos;
	return JsonError::Ok;
}

/* --- JsonDocument -------------------------------------------------------- */

JsonDocument::JsonDocument() = default;
JsonDocument::~JsonDocument() = default;
JsonDocument::JsonDocument(JsonDocument&&) noexcept = default;
JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;

JsonError JsonDocument::parse(std::string_view text)
{
	return parse(text, Limits());
}

JsonError JsonDocument::parse(std::string_view text, const Limits& limits)
{
	root.reset();
	error = JsonError::Ok;
	error_offset = 0;

	JsonParser parser(text, limits);
	std::unique_ptr<JsonValue> parsed;
	error = parser.parse(parsed, error_offset);

	/* A refused document hands out nothing. A half-built tree invites a caller
	   to read whatever happened to parse before the error, which is exactly the
	   part an attacker controls. */
	if (error == JsonError::Ok)
		root = std::move(parsed);

	return error;
}

/* --- JsonWriter ---------------------------------------------------------- */

std::string jsonEscape(std::string_view text)
{
	std::string out;
	out.reserve(text.size() + 2);
	out.push_back('"');

	for (unsigned char c : text)
	{
		switch (c)
		{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (c < 0x20)
				{
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else
				{
					/* Anything above 0x1F is passed through as written. The
					   input is assumed to be UTF-8; escaping it byte by byte
					   would corrupt every multi-byte character. */
					out.push_back(static_cast<char>(c));
				}
				break;
		}
	}

	out.push_back('"');
	return out;
}

JsonWriter::JsonWriter() = default;
JsonWriter::~JsonWriter() = default;

void JsonWriter::separate()
{
	if (stack.empty())
	{
		/* A second value at the top level would produce two documents in one
		   string, which is not what any caller means. */
		if (have_root)
			failed = true;
		have_root = true;
		return;
	}

	if (stack.back() == Frame::Object)
	{
		if (!expecting_value)
		{
			failed = true;  /* a value where a member name belongs */
			return;
		}
		/* Inside an object the separator belongs to the member name, which has
		   already written it and marked the frame populated. Doing it again
		   here is how the value ends up preceded by a comma of its own. */
		expecting_value = false;
		return;
	}

	if (populated.back())
		out.push_back(',');
	populated.back() = true;
}

JsonWriter& JsonWriter::beginObject()
{
	separate();
	out.push_back('{');
	stack.push_back(Frame::Object);
	populated.push_back(false);
	return *this;
}

JsonWriter& JsonWriter::endObject()
{
	if (stack.empty() || stack.back() != Frame::Object || expecting_value)
	{
		failed = true;
		return *this;
	}
	out.push_back('}');
	stack.pop_back();
	populated.pop_back();
	return *this;
}

JsonWriter& JsonWriter::beginArray()
{
	separate();
	out.push_back('[');
	stack.push_back(Frame::Array);
	populated.push_back(false);
	return *this;
}

JsonWriter& JsonWriter::endArray()
{
	if (stack.empty() || stack.back() != Frame::Array || expecting_value)
	{
		failed = true;
		return *this;
	}
	out.push_back(']');
	stack.pop_back();
	populated.pop_back();
	return *this;
}

JsonWriter& JsonWriter::member(std::string_view name)
{
	if (stack.empty() || stack.back() != Frame::Object || expecting_value)
	{
		failed = true;
		return *this;
	}
	if (populated.back())
		out.push_back(',');
	populated.back() = true;
	out += jsonEscape(name);
	out.push_back(':');
	expecting_value = true;
	return *this;
}

JsonWriter& JsonWriter::value(std::string_view text)
{
	separate();
	out += jsonEscape(text);
	return *this;
}

JsonWriter& JsonWriter::value(const char* text)
{
	return value(std::string_view(text ? text : ""));
}

JsonWriter& JsonWriter::value(int64_t number)
{
	separate();
	char buf[24];
	snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(number));
	out += buf;
	return *this;
}

JsonWriter& JsonWriter::value(double number)
{
	separate();
	if (!isfinite(number))
	{
		/* NaN and Infinity have no JSON spelling. Emitting the C one produces
		   a document that is not JSON, so this is a misuse rather than a value. */
		failed = true;
		return *this;
	}
	char buf[40];
	snprintf(buf, sizeof(buf), "%.17g", number);
	out += buf;
	return *this;
}

JsonWriter& JsonWriter::value(bool boolean)
{
	separate();
	out += boolean ? "true" : "false";
	return *this;
}

JsonWriter& JsonWriter::valueNull()
{
	separate();
	out += "null";
	return *this;
}

JsonWriter& JsonWriter::member(std::string_view name, std::string_view text)
{
	member(name);
	return value(text);
}

JsonWriter& JsonWriter::member(std::string_view name, const char* text)
{
	member(name);
	return value(text);
}

JsonWriter& JsonWriter::member(std::string_view name, int64_t number)
{
	member(name);
	return value(number);
}

JsonWriter& JsonWriter::member(std::string_view name, bool boolean)
{
	member(name);
	return value(boolean);
}

bool JsonWriter::isComplete() const
{
	return !failed && stack.empty() && have_root && !expecting_value;
}

std::string JsonWriter::str() const
{
	return isComplete() ? out : std::string();
}

}
}
