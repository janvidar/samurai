/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/json.h>
#include <string>
#include <string_view>
#include <vector>
#include <string.h>

/*
 * The JSON reader parses bytes from a network peer, so as much of this asserts
 * what is refused as what is parsed. The extensions other parsers accept -
 * comments, trailing commas, NaN, unquoted names - are each checked to be
 * refused, because accepting one means this reader and a conforming one
 * disagree about what a document said.
 *
 * Everything here is deterministic and none of it touches the network.
 */

namespace {

using Samurai::IO::JsonDocument;
using Samurai::IO::JsonError;
using Samurai::IO::JsonValue;
using Samurai::IO::JsonWriter;

bool accepts(std::string_view text)
{
	JsonDocument doc;
	return doc.parse(text) == JsonError::Ok && doc.getRoot() != nullptr;
}

bool refuses(std::string_view text)
{
	JsonDocument doc;
	const JsonError got = doc.parse(text);
	/* A refused document hands out nothing: a half-built tree would invite a
	   caller to read whatever happened to parse before the error. */
	return got != JsonError::Ok && doc.getRoot() == nullptr;
}

bool refuses_with(std::string_view text, JsonError expected)
{
	JsonDocument doc;
	return doc.parse(text) == expected && doc.getRoot() == nullptr;
}

} // namespace

EXO_TEST(json_parse_empty_object, {
	JsonDocument doc;
	return doc.parse("{}") == JsonError::Ok
	    && doc.getRoot() != nullptr
	    && doc.getRoot()->isObject()
	    && doc.getRoot()->size() == 0;
});

EXO_TEST(json_parse_empty_array, {
	JsonDocument doc;
	return doc.parse("[]") == JsonError::Ok && doc.getRoot()->isArray();
});

EXO_TEST(json_scalar_roots, {
	/* RFC 8259 allows any value as the document, not only an object. */
	return accepts("true") && accepts("false") && accepts("null")
	    && accepts("42") && accepts("\"text\"");
});

EXO_TEST(json_types, {
	JsonDocument doc;
	if (doc.parse("{\"b\":true,\"n\":1.5,\"i\":7,\"s\":\"x\",\"a\":[],\"o\":{},\"z\":null}")
	    != JsonError::Ok)
		return false;

	const JsonValue* r = doc.getRoot();
	return r->find("b")->isBoolean()
	    && r->find("n")->isNumber()
	    && r->find("i")->isNumber()
	    && r->find("s")->isString()
	    && r->find("a")->isArray()
	    && r->find("o")->isObject()
	    && r->find("z")->isNull();
});

EXO_TEST(json_typed_lookups, {
	JsonDocument doc;
	if (doc.parse("{\"port\":1511,\"name\":\"drop\",\"on\":true}") != JsonError::Ok)
		return false;

	const JsonValue* r = doc.getRoot();
	return r->findInteger("port") == 1511
	    && r->findString("name") == "drop"
	    && r->findBoolean("on")
	    /* An absent member gives the fallback rather than anything surprising. */
	    && r->findInteger("absent", -1) == -1
	    && r->findString("absent", "def") == "def";
});

EXO_TEST(json_wrong_type_gives_fallback, {
	/* A reader written against a schema the document half matches should get
	   an unsurprising value rather than a crash. */
	JsonDocument doc;
	if (doc.parse("{\"port\":\"1511\"}") != JsonError::Ok)
		return false;
	return doc.getRoot()->findInteger("port", -1) == -1;
});

EXO_TEST(json_integer_vs_double, {
	JsonDocument doc;
	if (doc.parse("{\"a\":7,\"b\":7.0,\"c\":7e0,\"d\":7.5}") != JsonError::Ok)
		return false;
	const JsonValue* r = doc.getRoot();

	/* Only a number written without a fraction or exponent is an integer.
	   1511.9 is a mistake at the sender, and truncating it hides that. */
	return r->findInteger("a", -1) == 7
	    && r->findInteger("b", -1) == -1
	    && r->findInteger("c", -1) == -1
	    && r->findInteger("d", -1) == -1
	    && r->findNumber("d") == 7.5;
});

EXO_TEST(json_negative_and_exponent, {
	JsonDocument doc;
	if (doc.parse("[-1,-1.5,1e3,1E-3,-0]") != JsonError::Ok)
		return false;
	const JsonValue* r = doc.getRoot();
	return r->at(0)->getInteger() == -1
	    && r->at(1)->getNumber() == -1.5
	    && r->at(2)->getNumber() == 1000.0
	    && r->at(4)->getInteger() == 0;
});

EXO_TEST(json_optional_distinguishes_absent, {
	JsonDocument doc;
	if (doc.parse("{\"a\":0,\"b\":\"\"}") != JsonError::Ok)
		return false;
	const JsonValue* r = doc.getRoot();

	/* Zero and empty are values, not absence: a control_port of 0 must be
	   distinguishable from one that was not sent. */
	return r->findOptionalInteger("a").has_value()
	    && r->findOptionalInteger("a").value() == 0
	    && !r->findOptionalInteger("missing").has_value()
	    && r->findOptionalString("b").has_value()
	    && !r->findOptionalString("missing").has_value();
});

EXO_TEST(json_nesting_and_arrays, {
	JsonDocument doc;
	if (doc.parse("{\"tcp\":[1511,1512],\"o\":{\"k\":[{\"deep\":1}]}}") != JsonError::Ok)
		return false;

	const JsonValue* r = doc.getRoot();
	const JsonValue* tcp = r->find("tcp");
	if (!tcp || tcp->size() != 2 || tcp->at(0)->getInteger() != 1511)
		return false;
	if (tcp->at(2) != nullptr)  /* out of range is null, not a crash */
		return false;

	const JsonValue* deep = r->find("o")->find("k")->at(0)->find("deep");
	return deep && deep->getInteger() == 1;
});

EXO_TEST(json_string_escapes, {
	JsonDocument doc;
	if (doc.parse("[\"a\\\"b\",\"a\\\\b\",\"a\\/b\",\"a\\nb\",\"a\\tb\"]") != JsonError::Ok)
		return false;
	const JsonValue* r = doc.getRoot();
	return r->at(0)->getString() == "a\"b"
	    && r->at(1)->getString() == "a\\b"
	    && r->at(2)->getString() == "a/b"
	    && r->at(3)->getString() == "a\nb"
	    && r->at(4)->getString() == "a\tb";
});

EXO_TEST(json_unicode_escapes, {
	JsonDocument doc;
	/* ASCII, two-byte, three-byte, and a surrogate pair for U+1F600. */
	if (doc.parse("[\"\\u0041\",\"\\u00e5\",\"\\u20ac\",\"\\ud83d\\ude00\"]")
	    != JsonError::Ok)
		return false;
	const JsonValue* r = doc.getRoot();
	return r->at(0)->getString() == "A"
	    && r->at(1)->getString() == "\xc3\xa5"
	    && r->at(2)->getString() == "\xe2\x82\xac"
	    && r->at(3)->getString() == "\xf0\x9f\x98\x80";
});

EXO_TEST(json_refuses_unpaired_surrogate, {
	/* Passing one through yields a string that is not valid UTF-8, and the
	   failure then surfaces somewhere with no idea where it came from. */
	return refuses("[\"\\ud83d\"]")          /* high with no low  */
	    && refuses("[\"\\ude00\"]")          /* lone low          */
	    && refuses("[\"\\ud83dA\"]")         /* high then no escape */
	    && refuses("[\"\\ud83d\\u0041\"]");  /* high then non-surrogate */
});

EXO_TEST(json_refuses_bad_escapes, {
	return refuses("[\"\\x41\"]")
	    && refuses("[\"\\u00\"]")
	    && refuses("[\"\\uZZZZ\"]")
	    && refuses("[\"\\\"]");
});

EXO_TEST(json_refuses_raw_control_character, {
	/* A raw newline inside a string would end a line-delimited framing that
	   the reader and the writer no longer agree on. */
	const char with_newline[] = "[\"a\nb\"]";
	const char with_tab[]     = "[\"a\tb\"]";
	return refuses(std::string_view(with_newline, sizeof(with_newline) - 1))
	    && refuses(std::string_view(with_tab, sizeof(with_tab) - 1));
});

EXO_TEST(json_refuses_the_usual_extensions, {
	return refuses("{\"a\":1,}")        /* trailing comma in an object */
	    && refuses("[1,2,]")            /* trailing comma in an array  */
	    && refuses("{a:1}")             /* unquoted member name        */
	    && refuses("{'a':1}")           /* single quotes               */
	    && refuses("[1] // comment")    /* comment                     */
	    && refuses("/* c */ [1]")
	    && refuses("[NaN]")
	    && refuses("[Infinity]")
	    && refuses("[+1]")              /* leading plus                */
	    && refuses("[01]")              /* leading zero                */
	    && refuses("[.5]")              /* no integer part             */
	    && refuses("[1.]")              /* no fraction digits          */
	    && refuses("[1e]");             /* no exponent digits          */
});

EXO_TEST(json_refuses_trailing_data, {
	/* Two documents in one string is not one document, however tempting it is
	   to return the first. */
	return refuses_with("{} {}", JsonError::TrailingData)
	    && refuses_with("[1][2]", JsonError::TrailingData)
	    && refuses_with("1 2", JsonError::TrailingData);
});

EXO_TEST(json_accepts_surrounding_whitespace, {
	return accepts("  \t\r\n {\"a\":1} \n ");
});

EXO_TEST(json_refuses_truncation, {
	return refuses_with("{", JsonError::Truncated)
	    && refuses_with("{\"a\"", JsonError::Truncated)
	    && refuses_with("[1,", JsonError::Truncated)
	    && refuses_with("\"unterminated", JsonError::Truncated)
	    && refuses_with("", JsonError::Truncated);
});

EXO_TEST(json_refuses_mismatched_closers, {
	return refuses("{]") && refuses("[}") && refuses("{\"a\":1]");
});

EXO_TEST(json_depth_is_a_limit_not_a_crash, {
	/* The point of the explicit stack. A million levels of nesting would smash
	   the call stack of a recursive parser; here it is an error code. */
	std::string deep(200000, '[');
	JsonDocument doc;
	const JsonError err = doc.parse(deep);
	return (err == JsonError::DepthExceeded || err == JsonError::SizeExceeded)
	    && doc.getRoot() == nullptr;
});

EXO_TEST(json_depth_limit_is_honoured, {
	JsonDocument::Limits limits;
	limits.maxDepth = 4;

	JsonDocument shallow;
	if (shallow.parse("[[[1]]]", limits) != JsonError::Ok)
		return false;

	JsonDocument deep;
	return deep.parse("[[[[[[1]]]]]]", limits) == JsonError::DepthExceeded;
});

EXO_TEST(json_size_limits_are_honoured, {
	JsonDocument::Limits limits;
	limits.maxDocumentSize = 16;
	JsonDocument doc;
	if (doc.parse("{\"aaaaaaaaaaaaaaaaaaaa\":1}", limits) != JsonError::SizeExceeded)
		return false;

	JsonDocument::Limits strings;
	strings.maxStringLength = 4;
	JsonDocument doc2;
	if (doc2.parse("[\"abcdefghij\"]", strings) != JsonError::SizeExceeded)
		return false;

	JsonDocument::Limits members;
	members.maxMembers = 2;
	JsonDocument doc3;
	return doc3.parse("{\"a\":1,\"b\":2,\"c\":3}", members) == JsonError::SizeExceeded;
});

EXO_TEST(json_duplicate_members_keep_the_first, {
	/* Discarding one of two spellings is how two readers of one document come
	   to disagree about what it said. Both are kept; find() takes the first. */
	JsonDocument doc;
	if (doc.parse("{\"a\":1,\"a\":2}") != JsonError::Ok)
		return false;
	return doc.getRoot()->size() == 2 && doc.getRoot()->findInteger("a") == 1;
});

EXO_TEST(json_skips_byte_order_mark, {
	return accepts("\xef\xbb\xbf{\"a\":1}");
});

EXO_TEST(json_error_offset_is_reported, {
	JsonDocument doc;
	if (doc.parse("{\"a\":1,,}") == JsonError::Ok)
		return false;
	return doc.getErrorOffset() > 0;
});

EXO_TEST(json_error_strings_exist, {
	const JsonError all[] = {
		JsonError::Ok, JsonError::Truncated, JsonError::Syntax,
		JsonError::DepthExceeded, JsonError::SizeExceeded,
		JsonError::TrailingData, JsonError::NumberRange,
	};
	for (JsonError e : all)
	{
		if (!Samurai::IO::toString(e) || !*Samurai::IO::toString(e))
			return false;
	}
	return true;
});

/*
 * Reading off the end of a truncated document is the failure a parser of
 * network input has to not have, so every prefix of a valid document is fed in
 * and asserted merely not to crash.
 */
EXO_TEST(json_every_prefix_is_safe, {
	const std::string doc =
		"{\"protocol\":\"DROP/1\",\"tests\":{\"ipv4\":[\"tcp\",\"udp\"]},"
		"\"limits\":{\"min_port\":1024},\"ok\":true,\"n\":null,\"f\":1.5e-3,"
		"\"esc\":\"a\\\"b\\u00e5\\ud83d\\ude00\"}";

	for (size_t i = 0; i <= doc.size(); i++)
	{
		JsonDocument d;
		const JsonError err = d.parse(std::string_view(doc).substr(0, i));
		if (err == JsonError::Ok && i != doc.size())
			return false;  /* a prefix must not parse as the whole */
		if (err != JsonError::Ok && d.getRoot() != nullptr)
			return false;
	}
	return true;
});

/* --- writer -------------------------------------------------------------- */

EXO_TEST(json_write_object, {
	JsonWriter w;
	w.beginObject()
	 .member("version", static_cast<int64_t>(1))
	 .member("client", "dropcheck/1.0")
	 .endObject();
	return w.isComplete() && w.str() == "{\"version\":1,\"client\":\"dropcheck/1.0\"}";
});

EXO_TEST(json_write_nested, {
	JsonWriter w;
	w.beginObject()
	 .member("tcp").beginArray().value(static_cast<int64_t>(1511))
	                            .value(static_cast<int64_t>(1512)).endArray()
	 .member("control_port", static_cast<int64_t>(51343))
	 .endObject();
	return w.str() == "{\"tcp\":[1511,1512],\"control_port\":51343}";
});

EXO_TEST(json_write_escapes, {
	JsonWriter w;
	w.beginObject().member("s", "a\"b\\c\nd").endObject();
	return w.str() == "{\"s\":\"a\\\"b\\\\c\\nd\"}";
});

EXO_TEST(json_write_control_characters, {
	JsonWriter w;
	w.beginObject().member("s", std::string_view("a\x01" "b", 3)).endObject();
	return w.str() == "{\"s\":\"a\\u0001b\"}";
});

EXO_TEST(json_write_passes_utf8_through, {
	/* Escaping byte by byte would corrupt every multi-byte character. */
	JsonWriter w;
	w.beginObject().member("s", "\xc3\xa5").endObject();
	return w.str() == "{\"s\":\"\xc3\xa5\"}";
});

EXO_TEST(json_write_booleans_and_null, {
	JsonWriter w;
	w.beginArray().value(true).value(false).valueNull().endArray();
	return w.str() == "[true,false,null]";
});

EXO_TEST(json_write_misuse_is_caught, {
	/* A mistake should surface as an empty result at the call site, not as
	   malformed JSON at the far end. */
	JsonWriter unclosed;
	unclosed.beginObject().member("a", static_cast<int64_t>(1));
	if (unclosed.isComplete() || !unclosed.str().empty())
		return false;

	JsonWriter mismatched;
	mismatched.beginObject().endArray();
	if (mismatched.isComplete())
		return false;

	JsonWriter valueWhereNameBelongs;
	valueWhereNameBelongs.beginObject().value("oops").endObject();
	if (valueWhereNameBelongs.isComplete())
		return false;

	JsonWriter nameOutsideObject;
	nameOutsideObject.beginArray().member("a").endArray();
	if (nameOutsideObject.isComplete())
		return false;

	JsonWriter twoRoots;
	twoRoots.beginObject().endObject();
	twoRoots.beginObject().endObject();
	return !twoRoots.isComplete();
});

EXO_TEST(json_write_rejects_non_finite, {
	JsonWriter w;
	w.beginArray().value(1.0 / 0.0).endArray();
	return !w.isComplete();
});

/* What the writer produces, the reader must accept. */
EXO_TEST(json_round_trip, {
	JsonWriter w;
	w.beginObject()
	 .member("s", "a\"b\nc\xc3\xa5")
	 .member("i", static_cast<int64_t>(-42))
	 .member("t", true)
	 .member("arr").beginArray().value("x").valueNull().endArray()
	 .endObject();

	JsonDocument doc;
	if (doc.parse(w.str()) != JsonError::Ok)
		return false;

	const JsonValue* r = doc.getRoot();
	return r->findString("s") == "a\"b\nc\xc3\xa5"
	    && r->findInteger("i") == -42
	    && r->findBoolean("t")
	    && r->find("arr")->size() == 2
	    && r->find("arr")->at(0)->getString() == "x"
	    && r->find("arr")->at(1)->isNull();
});
