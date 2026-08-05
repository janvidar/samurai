/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 * See the file "COPYING" for licensing details.
 */

#include <samurai/io/xml.h>
#include <string>
#include <string_view>
#include <vector>
#include <string.h>

/*
 * The XML reader exists to read a UPnP device description and a SOAP response,
 * both of which arrive from a device on the local network that has not been
 * authenticated. So as much of this asserts what is refused as what is parsed,
 * and the prefix sweep at the end is what covers reading off the end of a
 * truncated document.
 *
 * Everything here is deterministic and none of it touches the network.
 */

namespace {

using Samurai::IO::XmlDocument;
using Samurai::IO::XmlElement;
using Samurai::IO::XmlError;

/** Parse, and report only whether it was accepted. */
bool accepts(std::string_view text)
{
	XmlDocument doc;
	return doc.parse(text) == XmlError::Ok && doc.getRoot() != nullptr;
}

bool refuses_with(std::string_view text, XmlError expected)
{
	XmlDocument doc;
	const XmlError got = doc.parse(text);
	/* A refused document hands out nothing: a half-built tree would invite a
	   caller to read whatever happened to parse before the error. */
	return got == expected && doc.getRoot() == nullptr;
}

bool refuses(std::string_view text)
{
	XmlDocument doc;
	return doc.parse(text) != XmlError::Ok && doc.getRoot() == nullptr;
}

/* A cut-down device description with the WAN service nested as vendors nest it:
   the point is that the control URL has to be read from the same <service> as
   the matching <serviceType>, which is what a tree gives and a token stream
   does not. */
const char* IGD_DESCRIPTION =
	"<?xml version=\"1.0\"?>\n"
	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
	"  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
	"  <URLBase>http://192.168.1.1:5000/</URLBase>\n"
	"  <device>\n"
	"    <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>\n"
	"    <friendlyName>Test Router</friendlyName>\n"
	"    <serviceList>\n"
	"      <service>\n"
	"        <serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>\n"
	"        <controlURL>/ctl/L3F</controlURL>\n"
	"      </service>\n"
	"    </serviceList>\n"
	"    <deviceList>\n"
	"      <device>\n"
	"        <deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>\n"
	"        <deviceList>\n"
	"          <device>\n"
	"            <deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>\n"
	"            <serviceList>\n"
	"              <service>\n"
	"                <serviceType>\n"
	"                  urn:schemas-upnp-org:service:WANIPConnection:1\n"
	"                </serviceType>\n"
	"                <controlURL>/ctl/IPConn</controlURL>\n"
	"              </service>\n"
	"            </serviceList>\n"
	"          </device>\n"
	"        </deviceList>\n"
	"      </device>\n"
	"    </deviceList>\n"
	"  </device>\n"
	"</root>\n";

const char* SOAP_RESPONSE =
	"<?xml version=\"1.0\"?>\n"
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
	" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
	" <s:Body>\n"
	"  <u:GetExternalIPAddressResponse"
	" xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\n"
	"   <NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>\n"
	"  </u:GetExternalIPAddressResponse>\n"
	" </s:Body>\n"
	"</s:Envelope>\n";

const char* SOAP_FAULT =
	"<?xml version=\"1.0\"?>\n"
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
	" <s:Body>\n"
	"  <s:Fault>\n"
	"   <faultcode>s:Client</faultcode>\n"
	"   <faultstring>UPnPError</faultstring>\n"
	"   <detail>\n"
	"    <UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">\n"
	"     <errorCode>718</errorCode>\n"
	"     <errorDescription>ConflictInMappingEntry</errorDescription>\n"
	"    </UPnPError>\n"
	"   </detail>\n"
	"  </s:Fault>\n"
	" </s:Body>\n"
	"</s:Envelope>\n";

/** Find the control URL for a service type, the way the UPnP layer will. */
const XmlElement* find_service(const XmlElement* root, std::string_view wanted)
{
	if (!root) return nullptr;

	/* Every <service> anywhere in the tree, however deeply the vendor nested
	   the WAN connection device. */
	std::vector<const XmlElement*> pending;
	pending.push_back(root);

	while (!pending.empty())
	{
		const XmlElement* element = pending.back();
		pending.pop_back();

		for (const auto& child : element->getChildren())
		{
			if (child->getName() == "service" &&
			    child->getChildText("serviceType") == wanted)
				return child.get();

			pending.push_back(child.get());
		}
	}
	return nullptr;
}

}

/* ------------------------------------------------------------------------- */
/* What is parsed                                                            */
/* ------------------------------------------------------------------------- */

EXO_TEST(xml_parses_a_single_element,
{
	XmlDocument doc;
	if (doc.parse("<a/>") != XmlError::Ok) return false;
	return doc.getRoot() && doc.getRoot()->getName() == "a"
		&& doc.getRoot()->getChildren().empty();
});

EXO_TEST(xml_parses_nested_elements,
{
	XmlDocument doc;
	if (doc.parse("<a><b><c/></b></a>") != XmlError::Ok) return false;

	const XmlElement* b = doc.getRoot()->findChild("b");
	return b && b->findChild("c") && !doc.getRoot()->findChild("c");
});

EXO_TEST(xml_reads_character_data,
{
	XmlDocument doc;
	if (doc.parse("<a>hello</a>") != XmlError::Ok) return false;
	return doc.getRoot()->getText() == "hello";
});

/* Data separated by a child element is joined; nothing UPnP reads needs the
   halves apart. */
EXO_TEST(xml_joins_text_around_a_child,
{
	XmlDocument doc;
	if (doc.parse("<a>x<b/>y</a>") != XmlError::Ok) return false;
	return doc.getRoot()->getText() == "xy";
});

EXO_TEST(xml_attributes_in_either_quote,
{
	XmlDocument doc;
	if (doc.parse("<a one=\"1\" two='2'/>") != XmlError::Ok) return false;

	const XmlElement* root = doc.getRoot();
	return root->getAttribute("one") == "1"
		&& root->getAttribute("two") == "2"
		&& !root->getAttribute("three").has_value()
		&& root->getAttributes().size() == 2;
});

EXO_TEST(xml_attribute_may_be_empty,
{
	XmlDocument doc;
	if (doc.parse("<a empty=\"\"/>") != XmlError::Ok) return false;
	return doc.getRoot()->getAttribute("empty") == "";
});

EXO_TEST(xml_self_closing_element_has_no_children,
{
	XmlDocument doc;
	if (doc.parse("<a><b/><c/></a>") != XmlError::Ok) return false;
	return doc.getRoot()->getChildren().size() == 2;
});

EXO_TEST(xml_cdata_is_taken_literally,
{
	XmlDocument doc;
	if (doc.parse("<a><![CDATA[<b>&amp;</b>]]></a>") != XmlError::Ok) return false;
	return doc.getRoot()->getText() == "<b>&amp;</b>";
});

EXO_TEST(xml_comment_is_skipped,
{
	XmlDocument doc;
	if (doc.parse("<a><!-- <b/> -->text</a>") != XmlError::Ok) return false;
	return doc.getRoot()->getText() == "text" && doc.getRoot()->getChildren().empty();
});

EXO_TEST(xml_processing_instruction_is_skipped,
{
	return accepts("<?xml version=\"1.0\" encoding=\"UTF-8\"?><a/>")
		&& accepts("<a><?target data?></a>");
});

EXO_TEST(xml_predefined_entities_are_expanded,
{
	XmlDocument doc;
	if (doc.parse("<a>&amp;&lt;&gt;&quot;&apos;</a>") != XmlError::Ok) return false;
	return doc.getRoot()->getText() == "&<>\"'";
});

EXO_TEST(xml_entities_are_expanded_in_an_attribute,
{
	XmlDocument doc;
	if (doc.parse("<a v=\"a&amp;b\"/>") != XmlError::Ok) return false;
	return doc.getRoot()->getAttribute("v") == "a&b";
});

EXO_TEST(xml_numeric_character_reference_is_expanded,
{
	XmlDocument doc;
	if (doc.parse("<a>&#65;&#x42;&#X43;</a>") != XmlError::Ok) return false;
	return doc.getRoot()->getText() == "ABC";
});

EXO_TEST(xml_character_reference_becomes_utf8,
{
	XmlDocument doc;
	if (doc.parse("<a>&#xe5;</a>") != XmlError::Ok) return false;
	return doc.getRoot()->getText() == "\xc3\xa5";
});

/* A byte order mark is legitimate and says only that this is UTF-8. */
EXO_TEST(xml_bom_is_skipped,
{
	return accepts("\xef\xbb\xbf<a/>");
});

EXO_TEST(xml_whitespace_around_the_root_is_allowed,
{
	return accepts("\n\t <a/> \n");
});

/* ------------------------------------------------------------------------- */
/* Namespaces: prefixes are split off and matching is on the local name       */
/* ------------------------------------------------------------------------- */

EXO_TEST(xml_prefix_is_split_from_the_name,
{
	XmlDocument doc;
	if (doc.parse("<s:Envelope xmlns:s=\"x\"/>") != XmlError::Ok) return false;
	return doc.getRoot()->getName() == "Envelope" && doc.getRoot()->getPrefix() == "s";
});

EXO_TEST(xml_name_without_a_prefix_has_an_empty_one,
{
	XmlDocument doc;
	if (doc.parse("<Envelope/>") != XmlError::Ok) return false;
	return doc.getRoot()->getPrefix().empty();
});

/* This is what makes the reader usable against SOAP: vendors choose their own
   prefixes, so matching the written name matches nothing reliably. */
EXO_TEST(xml_find_child_ignores_the_prefix,
{
	XmlDocument doc;
	if (doc.parse("<s:Envelope><s:Body><u:Act/></s:Body></s:Envelope>") != XmlError::Ok)
		return false;

	const XmlElement* body = doc.getRoot()->findChild("Body");
	return body && body->findChild("Act");
});

EXO_TEST(xml_xmlns_is_an_ordinary_attribute,
{
	XmlDocument doc;
	if (doc.parse("<a xmlns=\"urn:x\" xmlns:s=\"urn:y\"/>") != XmlError::Ok) return false;
	return doc.getRoot()->getAttribute("xmlns") == "urn:x"
		&& doc.getRoot()->getAttribute("xmlns:s") == "urn:y";
});

/* ------------------------------------------------------------------------- */
/* Navigation                                                                */
/* ------------------------------------------------------------------------- */

EXO_TEST(xml_find_children_returns_every_match_in_order,
{
	XmlDocument doc;
	if (doc.parse("<a><b>1</b><c/><b>2</b></a>") != XmlError::Ok) return false;

	const std::vector<const XmlElement*> found = doc.getRoot()->findChildren("b");
	return found.size() == 2 && found[0]->getText() == "1" && found[1]->getText() == "2";
});

EXO_TEST(xml_find_descendant_reaches_below_the_children,
{
	XmlDocument doc;
	if (doc.parse("<a><b><c><d/></c></b></a>") != XmlError::Ok) return false;
	return doc.getRoot()->findDescendant("d") && !doc.getRoot()->findChild("d");
});

EXO_TEST(xml_find_descendant_is_breadth_first,
{
	XmlDocument doc;
	if (doc.parse("<a><b><t>deep</t></b><t>shallow</t></a>") != XmlError::Ok) return false;
	return doc.getRoot()->findDescendant("t")->getText() == "shallow";
});

EXO_TEST(xml_find_of_something_absent_is_null,
{
	XmlDocument doc;
	if (doc.parse("<a/>") != XmlError::Ok) return false;
	return !doc.getRoot()->findChild("b")
		&& !doc.getRoot()->findDescendant("b")
		&& doc.getRoot()->findChildren("b").empty();
});

/* A pretty-printed document indents its leaf values, so the raw text of a
   <controlURL> is normally wrapped in a newline and a run of spaces. */
EXO_TEST(xml_child_text_is_trimmed,
{
	XmlDocument doc;
	if (doc.parse("<a><b>\n   value \t</b></a>") != XmlError::Ok) return false;
	return doc.getRoot()->getChildText("b") == "value"
		&& doc.getRoot()->findChild("b")->getText() != "value";
});

EXO_TEST(xml_child_text_of_something_absent_is_empty,
{
	XmlDocument doc;
	if (doc.parse("<a/>") != XmlError::Ok) return false;
	return doc.getRoot()->getChildText("b").empty();
});

/* ------------------------------------------------------------------------- */
/* The two shapes UPnP actually sends                                        */
/* ------------------------------------------------------------------------- */

EXO_TEST(xml_reads_url_base_and_friendly_name,
{
	XmlDocument doc;
	if (doc.parse(IGD_DESCRIPTION) != XmlError::Ok) return false;

	const XmlElement* root = doc.getRoot();
	if (root->getChildText("URLBase") != "http://192.168.1.1:5000/") return false;

	const XmlElement* device = root->findChild("device");
	return device && device->getChildText("friendlyName") == "Test Router";
});

/*
 * The requirement that decided the tree over a token stream: the control URL
 * has to come from the same <service> as the matching <serviceType>, and that
 * service is nested three device levels down.
 */
EXO_TEST(xml_finds_a_service_control_url_by_service_type,
{
	XmlDocument doc;
	if (doc.parse(IGD_DESCRIPTION) != XmlError::Ok) return false;

	const XmlElement* wan = find_service(doc.getRoot(),
		"urn:schemas-upnp-org:service:WANIPConnection:1");
	if (!wan) return false;

	/* The serviceType in the fixture is written across three lines, as real
	   descriptions do, so this only matches because the text is trimmed. */
	return wan->getChildText("controlURL") == "/ctl/IPConn";
});

EXO_TEST(xml_does_not_confuse_two_services,
{
	XmlDocument doc;
	if (doc.parse(IGD_DESCRIPTION) != XmlError::Ok) return false;

	const XmlElement* l3f = find_service(doc.getRoot(),
		"urn:schemas-upnp-org:service:Layer3Forwarding:1");
	return l3f && l3f->getChildText("controlURL") == "/ctl/L3F";
});

EXO_TEST(xml_absent_service_is_not_found,
{
	XmlDocument doc;
	if (doc.parse(IGD_DESCRIPTION) != XmlError::Ok) return false;
	return !find_service(doc.getRoot(), "urn:schemas-upnp-org:service:Nonesuch:1");
});

EXO_TEST(xml_reads_a_soap_response_value,
{
	XmlDocument doc;
	if (doc.parse(SOAP_RESPONSE) != XmlError::Ok) return false;

	const XmlElement* body = doc.getRoot()->findChild("Body");
	if (!body) return false;

	const XmlElement* response = body->findChild("GetExternalIPAddressResponse");
	return response
		&& response->getChildText("NewExternalIPAddress") == "203.0.113.7";
});

EXO_TEST(xml_reads_a_soap_fault_error_code,
{
	XmlDocument doc;
	if (doc.parse(SOAP_FAULT) != XmlError::Ok) return false;

	const XmlElement* error = doc.getRoot()->findDescendant("UPnPError");
	return error
		&& error->getChildText("errorCode") == "718"
		&& error->getChildText("errorDescription") == "ConflictInMappingEntry";
});

/* ------------------------------------------------------------------------- */
/* What is refused                                                           */
/* ------------------------------------------------------------------------- */

/*
 * A document type declaration is refused outright rather than skipped. It is
 * what carries entity declarations, so refusing it closes external entity
 * expansion and the entity-recursion denial of service instead of trying to
 * survive them.
 */
EXO_TEST(xml_rejects_a_doctype,
{
	return refuses_with("<!DOCTYPE a><a/>", XmlError::Unsupported);
});

EXO_TEST(xml_rejects_an_internal_entity_declaration,
{
	return refuses_with(
		"<!DOCTYPE a [<!ENTITY x \"boom\">]><a>&x;</a>", XmlError::Unsupported);
});

EXO_TEST(xml_rejects_an_external_entity_declaration,
{
	return refuses_with(
		"<!DOCTYPE a [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><a>&x;</a>",
		XmlError::Unsupported);
});

/* A reference that is passed through untouched is one that whatever consumes
   the string may resolve instead. */
EXO_TEST(xml_rejects_an_unknown_entity,
{
	return refuses_with("<a>&nbsp;</a>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_an_unterminated_entity,
{
	return refuses("<a>&amp</a>");
});

EXO_TEST(xml_rejects_a_surrogate_character_reference,
{
	return refuses_with("<a>&#xD800;</a>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_a_character_reference_above_the_last_code_point,
{
	return refuses_with("<a>&#x110000;</a>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_a_null_character_reference,
{
	return refuses_with("<a>&#0;</a>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_a_non_numeric_character_reference,
{
	return refuses_with("<a>&#zz;</a>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_a_mismatched_end_tag,
{
	return refuses_with("<a></b>", XmlError::Mismatch);
});

EXO_TEST(xml_rejects_a_stray_end_tag,
{
	return refuses_with("<a/></a>", XmlError::Mismatch);
});

EXO_TEST(xml_rejects_an_unclosed_tag,
{
	return refuses_with("<a><b></a>", XmlError::Mismatch);
});

EXO_TEST(xml_rejects_an_unclosed_document,
{
	return refuses_with("<a><b>", XmlError::Truncated);
});

EXO_TEST(xml_rejects_two_root_elements,
{
	return refuses_with("<a/><b/>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_an_empty_document,
{
	return refuses_with("", XmlError::Truncated);
});

EXO_TEST(xml_rejects_text_outside_the_root,
{
	return refuses_with("junk<a/>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_an_unquoted_attribute,
{
	return refuses_with("<a v=1/>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_an_attribute_with_no_value,
{
	return refuses_with("<a v/>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_a_raw_less_than_in_an_attribute,
{
	return refuses_with("<a v=\"<\"/>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_a_name_starting_with_a_digit,
{
	return refuses_with("<1a/>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_a_double_hyphen_in_a_comment,
{
	return refuses_with("<a><!-- -- --></a>", XmlError::Syntax);
});

EXO_TEST(xml_rejects_an_unterminated_comment,
{
	return refuses_with("<a><!-- oops</a>", XmlError::Truncated);
});

EXO_TEST(xml_rejects_unterminated_cdata,
{
	return refuses_with("<a><![CDATA[oops</a>", XmlError::Truncated);
});

/* ------------------------------------------------------------------------- */
/* Limits                                                                    */
/*                                                                           */
/* Defences rather than tuning knobs: each one bounds what a hostile document */
/* can cost.                                                                 */
/* ------------------------------------------------------------------------- */

EXO_TEST(xml_rejects_a_document_over_the_size_limit,
{
	XmlDocument::Limits limits;
	limits.maxDocumentSize = 8;

	XmlDocument doc;
	return doc.parse("<a>0123456789</a>", limits) == XmlError::SizeExceeded;
});

EXO_TEST(xml_rejects_a_document_over_the_depth_limit,
{
	std::string deep;
	for (int n = 0; n < 64; n++) deep += "<a>";
	deep += "x";
	for (int n = 0; n < 64; n++) deep += "</a>";

	XmlDocument::Limits limits;
	limits.maxDepth = 16;

	XmlDocument doc;
	if (doc.parse(deep, limits) != XmlError::DepthExceeded) return false;

	/* The same document is fine when the limit allows it. */
	limits.maxDepth = 128;
	XmlDocument deeper;
	return deeper.parse(deep, limits) == XmlError::Ok;
});

EXO_TEST(xml_rejects_too_many_elements,
{
	std::string many = "<a>";
	for (int n = 0; n < 64; n++) many += "<b/>";
	many += "</a>";

	XmlDocument::Limits limits;
	limits.maxElements = 16;

	XmlDocument doc;
	return doc.parse(many, limits) == XmlError::SizeExceeded;
});

EXO_TEST(xml_rejects_too_many_attributes,
{
	std::string wide = "<a";
	for (int n = 0; n < 40; n++) wide += " x" + std::to_string(n) + "=\"1\"";
	wide += "/>";

	XmlDocument::Limits limits;
	limits.maxAttributes = 8;

	XmlDocument doc;
	return doc.parse(wide, limits) == XmlError::SizeExceeded;
});

EXO_TEST(xml_rejects_text_over_the_limit,
{
	XmlDocument::Limits limits;
	limits.maxTextLength = 4;

	XmlDocument doc;
	return doc.parse("<a>0123456789</a>", limits) == XmlError::SizeExceeded;
});

EXO_TEST(xml_rejects_a_name_over_the_limit,
{
	XmlDocument::Limits limits;
	limits.maxNameLength = 4;

	XmlDocument doc;
	return doc.parse("<abcdefghij/>", limits) == XmlError::SizeExceeded;
});

/* ------------------------------------------------------------------------- */
/* Truncation sweep                                                          */
/*                                                                           */
/* Every prefix of a valid document, which is what an index-based parser gets  */
/* wrong: one missing bound and a short read walks off the end. Under the      */
/* sanitizer build this is where that shows up, deterministically and on any   */
/* machine. Nothing is asserted about which error comes back - only that the   */
/* parser answers rather than crashing, and hands out no tree when it fails.   */
/* ------------------------------------------------------------------------- */

namespace {

bool sweep_prefixes(std::string_view document)
{
	for (size_t n = 0; n <= document.size(); n++)
	{
		XmlDocument doc;
		const XmlError result = doc.parse(document.substr(0, n));

		switch (result)
		{
			case XmlError::Ok:
				/* Only the whole document may parse; a prefix that does is a
				   prefix that happened to be well formed, which is fine. */
				if (!doc.getRoot()) return false;
				break;

			case XmlError::Truncated:
			case XmlError::Syntax:
			case XmlError::Mismatch:
			case XmlError::DepthExceeded:
			case XmlError::SizeExceeded:
			case XmlError::Unsupported:
				if (doc.getRoot()) return false;
				break;

			default:
				return false;
		}
	}
	return true;
}

}

EXO_TEST(xml_every_prefix_of_a_device_description_is_answered,
{
	return sweep_prefixes(IGD_DESCRIPTION);
});

EXO_TEST(xml_every_prefix_of_a_soap_response_is_answered,
{
	return sweep_prefixes(SOAP_RESPONSE);
});

EXO_TEST(xml_every_prefix_of_a_soap_fault_is_answered,
{
	return sweep_prefixes(SOAP_FAULT);
});

/* Truncation inside each construct that scans forward for a terminator. */
EXO_TEST(xml_every_prefix_of_the_awkward_constructs_is_answered,
{
	return sweep_prefixes("<a><![CDATA[body]]></a>")
		&& sweep_prefixes("<a><!-- comment --></a>")
		&& sweep_prefixes("<a v=\"value\" w='other'/>")
		&& sweep_prefixes("<a>&amp;&#x41;&#66;</a>")
		&& sweep_prefixes("<?xml version=\"1.0\"?><s:a xmlns:s=\"urn:x\"><b/></s:a>")
		&& sweep_prefixes("\xef\xbb\xbf<a/>");
});

/* The error offset is inside the document, so a diagnostic can point at it. */
EXO_TEST(xml_error_offset_is_within_the_document,
{
	const std::string text = "<a><b></a>";
	XmlDocument doc;
	if (doc.parse(text) == XmlError::Ok) return false;
	return doc.getErrorOffset() <= text.size();
});

EXO_TEST(xml_tostring_names_every_error,
{
	const XmlError all[] = {
		XmlError::Ok, XmlError::Truncated, XmlError::Syntax, XmlError::Mismatch,
		XmlError::DepthExceeded, XmlError::SizeExceeded, XmlError::Unsupported };

	for (const XmlError error : all)
	{
		const char* name = Samurai::IO::toString(error);
		if (!name || !*name) return false;
	}
	return true;
});

/* A document may be reparsed, and a failure must not leave the previous tree
   reachable. */
EXO_TEST(xml_reparsing_replaces_the_previous_tree,
{
	XmlDocument doc;
	if (doc.parse("<first/>") != XmlError::Ok) return false;
	if (doc.getRoot()->getName() != "first") return false;

	if (doc.parse("<second/>") != XmlError::Ok) return false;
	if (doc.getRoot()->getName() != "second") return false;

	return doc.parse("<broken>") != XmlError::Ok && doc.getRoot() == nullptr;
});

/* ------------------------------------------------------------------------- */
/* The same document, read as events                                         */
/* ------------------------------------------------------------------------- */

namespace {

using Samurai::IO::XmlContentHandler;
using Samurai::IO::XmlReader;

/* Writes down what it is told, so a case can assert the sequence. */
class Recorder final : public XmlContentHandler
{
	public:
		std::vector<std::string> events;
		int documents_started = 0;
		int documents_ended = 0;

		void startDocument() override { documents_started++; }
		void endDocument() override { documents_ended++; }

		void startElement(std::string_view prefix, std::string_view name,
			const Attributes& attributes) override
		{
			std::string line = "start ";
			if (!prefix.empty()) line += std::string(prefix) + ":";
			line += name;
			for (const auto& a : attributes)
				line += " " + a.first + "=" + a.second;
			events.push_back(line);
		}

		void endElement(std::string_view prefix, std::string_view name) override
		{
			std::string line = "end ";
			if (!prefix.empty()) line += std::string(prefix) + ":";
			line += name;
			events.push_back(line);
		}

		void characters(std::string_view text) override
		{
			events.push_back("text " + std::string(text));
		}

		/* The character data of the whole document, joined. */
		std::string allText() const
		{
			std::string out;
			for (const std::string& e : events)
				if (e.compare(0, 5, "text ") == 0) out += e.substr(5);
			return out;
		}
};

/*
 * How much character data the tree holds, everywhere in it.
 *
 * Not the data itself: the tree cannot reproduce document order, because an
 * element joins the data written either side of a child - <a>x<b>y</b>z</a> holds
 * "xz" on a and "y" on b, where the events are "x", "y", "z" in that order. What
 * both must agree on is that nothing was dropped or delivered twice.
 */
size_t treeTextLength(const XmlElement* element)
{
	if (!element) return 0;

	size_t total = element->getText().size();
	for (const auto& child : element->getChildren())
		total += treeTextLength(child.get());
	return total;
}

std::string joined(const std::vector<std::string>& events)
{
	std::string out;
	for (const std::string& e : events) { out += e; out += "|"; }
	return out;
}

}

/* The events, in the order the document was written. */
EXO_TEST(xml_reader_reports_a_document_in_order,
{
	Recorder r;
	EXO_ASSERT(XmlReader::parse("<a x=\"1\"><b>text</b></a>", r) == XmlError::Ok);

	EXO_ASSERT(joined(r.events) ==
		"start a x=1|start b|text text|end b|end a|");
	EXO_ASSERT(r.documents_started == 1);
	EXO_ASSERT(r.documents_ended == 1);
	return 1;
});

/* An empty element is a start and an end with nothing between. */
EXO_TEST(xml_reader_reports_an_empty_element_as_start_and_end,
{
	Recorder r;
	EXO_ASSERT(XmlReader::parse("<a><b/></a>", r) == XmlError::Ok);
	EXO_ASSERT(joined(r.events) == "start a|start b|end b|end a|");
	return 1;
});

/* A prefix is reported apart from the name, as the tree reports it. */
EXO_TEST(xml_reader_reports_a_prefix_apart_from_the_name,
{
	Recorder r;
	EXO_ASSERT(XmlReader::parse("<s:Body xmlns:s=\"u\"/>", r) == XmlError::Ok);
	EXO_ASSERT(joined(r.events) == "start s:Body xmlns:s=u|end s:Body|");
	return 1;
});

namespace {

size_t textPieces(const Recorder& r)
{
	size_t pieces = 0;
	for (const std::string& e : r.events)
		if (e.compare(0, 5, "text ") == 0) pieces++;
	return pieces;
}

}

/*
 * References are expanded before the handler sees them, and expanding one does
 * not break the run it was written in: a handler is given the whole of a run
 * between markup as one call.
 */
EXO_TEST(xml_reader_expands_references_without_splitting_a_run,
{
	Recorder r;
	EXO_ASSERT(XmlReader::parse("<a>one &amp; two</a>", r) == XmlError::Ok);

	EXO_ASSERT(r.allText() == "one & two");
	EXO_ASSERT_EQ_UINT(textPieces(r), 1u);
	return 1;
});

/*
 * What does split a run is markup: a CDATA section is its own piece, so an
 * element's data can arrive in several and a handler that wants all of it has to
 * join them. This is the case the documented contract exists for.
 */
EXO_TEST(xml_reader_splits_a_runs_text_at_markup,
{
	Recorder r;
	EXO_ASSERT(XmlReader::parse("<a>x<![CDATA[y]]>z</a>", r) == XmlError::Ok);

	EXO_ASSERT(r.allText() == "xyz");
	EXO_ASSERT_EQ_UINT(textPieces(r), 3u);
	return 1;
});

/* CDATA arrives as character data, unexpanded. */
EXO_TEST(xml_reader_reports_cdata_as_characters,
{
	Recorder r;
	EXO_ASSERT(XmlReader::parse("<a><![CDATA[<b>&amp;]]></a>", r) == XmlError::Ok);
	EXO_ASSERT(r.allText() == "<b>&amp;");
	return 1;
});

/*
 * A document that failed is not reported as having ended: a handler told the
 * document ended has been told the events it saw were all of them, and for a
 * failed parse they were not.
 */
EXO_TEST(xml_reader_does_not_end_a_document_that_failed,
{
	Recorder r;
	EXO_ASSERT(XmlReader::parse("<a><b></a>", r) != XmlError::Ok);
	EXO_ASSERT(r.documents_started == 1);
	EXO_ASSERT(r.documents_ended == 0);
	return 1;
});

/* And it says where it stopped, when asked. */
EXO_TEST(xml_reader_reports_where_it_stopped,
{
	Recorder r;
	const XmlReader::Limits limits;
	size_t offset = 0;

	EXO_ASSERT(XmlReader::parse("<a>text<<</a>", r, limits, &offset) != XmlError::Ok);
	EXO_ASSERT(offset > 0);
	EXO_ASSERT(offset <= strlen("<a>text<<</a>"));
	return 1;
});

/*
 * The point of one parser behind both: whatever the tree accepts, the events
 * accept, and whatever it refuses they refuse with the same reason. A second
 * hand-written parser is exactly how those two drift apart.
 */
EXO_TEST(xml_reader_and_document_agree_on_every_case_here,
{
	static const char* documents[] = {
		"<a/>",
		"<a></a>",
		"<a x=\"1\" y=\"2\"/>",
		"<a><b><c>deep</c></b></a>",
		"<a>one &amp; two &lt;three&gt;</a>",
		"<a><![CDATA[raw]]></a>",
		"<!-- comment --><a/>",
		"<?xml version=\"1.0\"?><a/>",
		"\xef\xbb\xbf<a/>",
		"<s:a xmlns:s=\"u\"><s:b/></s:a>",
		"<a>&#65;&#x42;</a>",
		/* And the refusals. */
		"",
		"<a>",
		"<a></b>",
		"<a><b></a>",
		"<a/><b/>",
		"<a>&nosuch;</a>",
		"<a x=\"<\"/>",
		"<!DOCTYPE a><a/>",
		"<a",
		"</a>",
		0
	};

	for (size_t n = 0; documents[n]; n++)
	{
		XmlDocument doc;
		const XmlError tree = doc.parse(documents[n]);

		Recorder r;
		const XmlError events = XmlReader::parse(documents[n], r);

		if (tree != events)
		{
			const std::string why = std::string("'") + documents[n]
				+ "' is " + Samurai::IO::toString(tree) + " as a tree but "
				+ Samurai::IO::toString(events) + " as events";
			EXO_FAIL(why.c_str());
		}
	}
	return 1;
});

/* And they agree on the text they saw, however it was split. */
EXO_TEST(xml_reader_and_document_agree_on_the_text,
{
	static const char* documents[] = {
		"<a>plain</a>",
		"<a>one &amp; two</a>",
		"<a><![CDATA[raw]]>and more</a>",
		"<a>before<b/>after</a>",
		"<a>&#65;&#x42;C</a>",
		0
	};

	for (size_t n = 0; documents[n]; n++)
	{
		XmlDocument doc;
		EXO_ASSERT(doc.parse(documents[n]) == XmlError::Ok);

		Recorder r;
		EXO_ASSERT(XmlReader::parse(documents[n], r) == XmlError::Ok);

		if (doc.getRoot()->getText() != r.allText())
		{
			const std::string why = std::string("'") + documents[n] + "': tree says '"
				+ doc.getRoot()->getText() + "', events say '" + r.allText() + "'";
			EXO_FAIL(why.c_str());
		}
	}
	return 1;
});

/* ------------------------------------------------------------------------- */
/* What bounds an event parse                                                */
/* ------------------------------------------------------------------------- */

/*
 * Nothing is retained, so the limits are what stop a hostile document rather than
 * memory running out - and they have to hold for the events exactly as they do for
 * the tree, or raising them for one would quietly raise them for neither.
 */
EXO_TEST(xml_reader_refuses_a_document_past_its_size,
{
	Recorder r;
	XmlReader::Limits limits;
	limits.maxDocumentSize = 8;

	EXO_ASSERT(XmlReader::parse("<a>0123456789</a>", r, limits)
		== XmlError::SizeExceeded);
	return 1;
});

EXO_TEST(xml_reader_refuses_a_document_nested_too_deep,
{
	Recorder r;
	XmlReader::Limits limits;
	limits.maxDepth = 2;

	EXO_ASSERT(XmlReader::parse("<a><b><c/></b></a>", r, limits)
		== XmlError::DepthExceeded);
	return 1;
});

EXO_TEST(xml_reader_refuses_too_many_elements,
{
	Recorder r;
	XmlReader::Limits limits;
	limits.maxElements = 2;

	EXO_ASSERT(XmlReader::parse("<a><b/><c/></a>", r, limits)
		== XmlError::SizeExceeded);
	return 1;
});

EXO_TEST(xml_reader_refuses_too_many_attributes,
{
	Recorder r;
	XmlReader::Limits limits;
	limits.maxAttributes = 1;

	EXO_ASSERT(XmlReader::parse("<a x=\"1\" y=\"2\"/>", r, limits)
		== XmlError::SizeExceeded);
	return 1;
});

/*
 * Character data is counted per element and across the runs it was written in, so
 * text either side of a child adds up rather than each piece being measured on
 * its own. A per-run cap would let a document carry any amount of data by
 * breaking it up.
 */
EXO_TEST(xml_reader_counts_an_elements_text_across_its_runs,
{
	Recorder r;
	XmlReader::Limits limits;
	limits.maxTextLength = 8;

	/* Four runs of three, which is over the cap only when they are added up. */
	EXO_ASSERT(XmlReader::parse("<a>aaa<b/>bbb<c/>ccc<d/>ddd</a>", r, limits)
		== XmlError::SizeExceeded);

	/* And within it when they are not. */
	Recorder ok;
	EXO_ASSERT(XmlReader::parse("<a>aaa<b/>bbb</a>", ok, limits) == XmlError::Ok);
	EXO_ASSERT(ok.allText() == "aaabbb");
	return 1;
});

/*
 * The corpus QuickDC's own reader tests were built on.
 *
 * That reader is gone - this one replaced it - and its cases were about a parser
 * that no longer exists, so their expectations went with it. Their inputs did not:
 * sixty-three documents that a real consumer's tests were written against are
 * worth more as a corpus than as nothing, and what is asserted over them is the
 * property that matters here - the tree and the events agree, on acceptance, on
 * refusal, and on the reason - plus that none of them takes the parser down.
 */
EXO_TEST(xml_the_inherited_corpus_reads_the_same_both_ways,
{
	static const char* documents[] = {
		"<a></a>",
		"<a>text</a>",
		"<a><b>text</b></a>",
		"<?xml version=\"1.0\"?><!-- note --><root></root>",
		"<a x=\"1\"></a>",
		"<a x='one'></a>",
		"<a x=\"1\" y=\"2\" z=\"3\"></a>",
		"<a x=\"\"></a>",
		"<a x=\"&amp;&lt;&gt;&quot;&apos;\"></a>",
		"<a href=\"http://example.org/x\"></a>",
		"<a/>",
		"<a x=\"1\"/>",
		"<a x=\"1\" />",
		"<a standalone/>",
		"<a><b x=\"1\"/></a>",
		"<a x=\"1\"></a><b></b>",
		"<a 'foo'></a>",
		"<a \"foo\"></a>",
		"<a \"foo\" x=\"1\"></a>",
		"<a x =\"1\"></a>",
		"<a x= \"1\"></a>",
		"<a x = \"1\"></a>",
		"<a w=\"0\" x = \"1\" y=\"2\"></a>",
		"<a x = \"1\"/>",
		"<a x\n=\n\"1\"></a>",
		"<a x y=\"1\"></a>",
		"<a x ></a>",
		"<a x=1></a>",
		"<a x=1 y=2></a>",
		"<a x=\"p>q\"></a>",
		"<a x=\"it's\"></a>",
		"<a href=http://example.org/x></a>",
		"<a href=http://example.org/x/>",
		"<a x=p/q></a>",
		"<a x=/root></a>",
		"<a x=/>",
		"<a>&lt;b&gt; &amp; &quot;c&quot;</a>",
		"<a></a>tail",
		"<a><!-- p > q --><b></b></a>",
		"<a><!----></a>",
		"<a><!-- <b></b> --></a>",
		"<a><![CDATA[p>q & <b>]]></a>",
		"<a><![CDATA[]]></a>",
		"<!DOCTYPE a><a></a>",
		"<a x=\"\xc3\xa6\xc3\xb8\xc3\xa5\"></a>",
		"<a>\xc3\xa6\xc3\xb8\xc3\xa5</a>",
		"<a>&#65;&#66;</a>",
		"<a>&#x41;&#X42;</a>",
		"<a x=\"&#65;\"></a>",
		"<a>&#229;</a>",
		"<a>&#x263A;</a>",
		"<a>&#x1F600;</a>",
		"<a>a&#66;c</a>",
		"<a>a &amp; b</a>",
		"<a>&nbsp;</a>",
		"<a>&#;</a>",
		"<a>&#xZZ;</a>",
		"<a>&#x110000;</a>",
		"<a>&#xD800;</a>",
		"<a>&#0;</a>",
		"<a>&#65</a>",
		"<a x=\"&amp;lt;\">&amp;gt;</a>",
		"<a><![CDATA[&#65;&amp;]]></a>",
		0
	};

	size_t accepted = 0;
	size_t refused = 0;

	for (size_t n = 0; documents[n]; n++)
	{
		XmlDocument doc;
		const XmlError tree = doc.parse(documents[n]);

		Recorder r;
		const XmlError events = XmlReader::parse(documents[n], r);

		if (tree != events)
		{
			const std::string why = std::string("'") + documents[n] + "' is "
				+ Samurai::IO::toString(tree) + " as a tree but "
				+ Samurai::IO::toString(events) + " as events";
			EXO_FAIL(why.c_str());
		}

		if (tree == XmlError::Ok)
		{
			accepted++;
			if (treeTextLength(doc.getRoot()) != r.allText().size())
			{
				const std::string why = std::string("'") + documents[n] + "': the tree holds "
					+ std::to_string(treeTextLength(doc.getRoot()))
					+ " characters against " + std::to_string(r.allText().size())
					+ " reported as events";
				EXO_FAIL(why.c_str());
			}
		}
		else refused++;
	}

	/* Both kinds are represented, or this would be asserting one thing twice. */
	EXO_ASSERT(accepted > 8);
	EXO_ASSERT(refused > 8);
	return 1;
});
