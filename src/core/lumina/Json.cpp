/*
 * Copyright 2026, Lumina for Haiku contributors.
 * Distributed under the terms of the MIT License.
 */
#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lumina {

std::string
JsonError::ToString() const
{
	return std::to_string(line) + ":" + std::to_string(column) + ": "
		+ message;
}


namespace {

class Parser {
public:
	Parser(const std::string& text) : fText(text) {}

	bool Parse(Value& out, JsonError* error)
	{
		bool ok = _SkipSpace() && _ParseValue(out, 0);
		if (ok) {
			ok = _SkipSpace();
			if (ok && fPos != fText.size())
				ok = _Fail("unexpected trailing characters");
		}
		if (!ok && error != nullptr)
			*error = fError;
		return ok;
	}

private:
	static const int kMaxDepth = 64;

	bool _Fail(const std::string& message)
	{
		if (fError.message.empty()) {
			fError.message = message;
			int line = 1, column = 1;
			for (size_t i = 0; i < fPos && i < fText.size(); i++) {
				if (fText[i] == '\n') {
					line++;
					column = 1;
				} else
					column++;
			}
			fError.line = line;
			fError.column = column;
		}
		return false;
	}

	bool _AtEnd() const { return fPos >= fText.size(); }
	char _Peek() const { return _AtEnd() ? '\0' : fText[fPos]; }

	bool _SkipSpace()
	{
		while (!_AtEnd()) {
			char c = fText[fPos];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				fPos++;
			} else if (c == '/' && fPos + 1 < fText.size()
				&& fText[fPos + 1] == '/') {
				while (!_AtEnd() && fText[fPos] != '\n')
					fPos++;
			} else if (c == '/' && fPos + 1 < fText.size()
				&& fText[fPos + 1] == '*') {
				size_t end = fText.find("*/", fPos + 2);
				if (end == std::string::npos)
					return _Fail("unterminated block comment");
				fPos = end + 2;
			} else
				break;
		}
		return true;
	}

	bool _ParseValue(Value& out, int depth)
	{
		if (depth > kMaxDepth)
			return _Fail("nesting too deep");
		if (_AtEnd())
			return _Fail("unexpected end of input");
		char c = _Peek();
		switch (c) {
			case '{': return _ParseObject(out, depth);
			case '[': return _ParseArray(out, depth);
			case '"':
			{
				std::string s;
				if (!_ParseString(s))
					return false;
				out = Value(std::move(s));
				return true;
			}
			case 't': return _ParseLiteral("true", Value(true), out);
			case 'f': return _ParseLiteral("false", Value(false), out);
			case 'n': return _ParseLiteral("null", Value(), out);
			default:
				if (c == '-' || (c >= '0' && c <= '9'))
					return _ParseNumber(out);
				return _Fail(std::string("unexpected character '") + c + "'");
		}
	}

	bool _ParseLiteral(const char* word, Value v, Value& out)
	{
		std::string w(word);
		if (fText.compare(fPos, w.size(), w) != 0)
			return _Fail("invalid literal");
		fPos += w.size();
		out = std::move(v);
		return true;
	}

	bool _ParseNumber(Value& out)
	{
		size_t start = fPos;
		if (_Peek() == '-')
			fPos++;
		if (!(_Peek() >= '0' && _Peek() <= '9'))
			return _Fail("invalid number");
		while (_Peek() >= '0' && _Peek() <= '9')
			fPos++;
		if (_Peek() == '.') {
			fPos++;
			if (!(_Peek() >= '0' && _Peek() <= '9'))
				return _Fail("invalid number fraction");
			while (_Peek() >= '0' && _Peek() <= '9')
				fPos++;
		}
		if (_Peek() == 'e' || _Peek() == 'E') {
			fPos++;
			if (_Peek() == '+' || _Peek() == '-')
				fPos++;
			if (!(_Peek() >= '0' && _Peek() <= '9'))
				return _Fail("invalid number exponent");
			while (_Peek() >= '0' && _Peek() <= '9')
				fPos++;
		}
		std::string number = fText.substr(start, fPos - start);
		out = Value(std::strtod(number.c_str(), nullptr));
		return true;
	}

	static void _AppendUtf8(std::string& s, uint32_t cp)
	{
		if (cp < 0x80) {
			s += char(cp);
		} else if (cp < 0x800) {
			s += char(0xc0 | (cp >> 6));
			s += char(0x80 | (cp & 0x3f));
		} else if (cp < 0x10000) {
			s += char(0xe0 | (cp >> 12));
			s += char(0x80 | ((cp >> 6) & 0x3f));
			s += char(0x80 | (cp & 0x3f));
		} else {
			s += char(0xf0 | (cp >> 18));
			s += char(0x80 | ((cp >> 12) & 0x3f));
			s += char(0x80 | ((cp >> 6) & 0x3f));
			s += char(0x80 | (cp & 0x3f));
		}
	}

	bool _ParseHex4(uint32_t& value)
	{
		if (fPos + 4 > fText.size())
			return _Fail("truncated \\u escape");
		value = 0;
		for (int i = 0; i < 4; i++) {
			char c = fText[fPos++];
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= uint32_t(c - '0');
			else if (c >= 'a' && c <= 'f')
				value |= uint32_t(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				value |= uint32_t(c - 'A' + 10);
			else
				return _Fail("invalid \\u escape");
		}
		return true;
	}

	bool _ParseString(std::string& out)
	{
		fPos++;	// opening quote
		while (true) {
			if (_AtEnd())
				return _Fail("unterminated string");
			char c = fText[fPos++];
			if (c == '"')
				return true;
			if (c == '\n')
				return _Fail("newline in string");
			if (c != '\\') {
				out += c;
				continue;
			}
			if (_AtEnd())
				return _Fail("unterminated escape");
			char e = fText[fPos++];
			switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					uint32_t cp;
					if (!_ParseHex4(cp))
						return false;
					if (cp >= 0xd800 && cp <= 0xdbff) {
						if (fText.compare(fPos, 2, "\\u") != 0)
							return _Fail("unpaired surrogate");
						fPos += 2;
						uint32_t low;
						if (!_ParseHex4(low))
							return false;
						if (low < 0xdc00 || low > 0xdfff)
							return _Fail("invalid surrogate pair");
						cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
					}
					_AppendUtf8(out, cp);
					break;
				}
				default:
					return _Fail("invalid escape");
			}
		}
	}

	bool _ParseArray(Value& out, int depth)
	{
		fPos++;
		out = Value::MakeArray();
		if (!_SkipSpace())
			return false;
		if (_Peek() == ']') {
			fPos++;
			return true;
		}
		while (true) {
			Value item;
			if (!_ParseValue(item, depth + 1))
				return false;
			out.Push(std::move(item));
			if (!_SkipSpace())
				return false;
			char c = _Peek();
			if (c == ',') {
				fPos++;
				if (!_SkipSpace())
					return false;
				if (_Peek() == ']') {	// trailing comma
					fPos++;
					return true;
				}
				continue;
			}
			if (c == ']') {
				fPos++;
				return true;
			}
			return _Fail("expected ',' or ']'");
		}
	}

	bool _ParseObject(Value& out, int depth)
	{
		fPos++;
		out = Value::MakeObject();
		if (!_SkipSpace())
			return false;
		if (_Peek() == '}') {
			fPos++;
			return true;
		}
		while (true) {
			if (_Peek() != '"')
				return _Fail("expected string key");
			std::string key;
			if (!_ParseString(key))
				return false;
			if (!_SkipSpace())
				return false;
			if (_Peek() != ':')
				return _Fail("expected ':'");
			fPos++;
			if (!_SkipSpace())
				return false;
			Value item;
			if (!_ParseValue(item, depth + 1))
				return false;
			out[key] = std::move(item);	// duplicate keys: last wins
			if (!_SkipSpace())
				return false;
			char c = _Peek();
			if (c == ',') {
				fPos++;
				if (!_SkipSpace())
					return false;
				if (_Peek() == '}') {
					fPos++;
					return true;
				}
				continue;
			}
			if (c == '}') {
				fPos++;
				return true;
			}
			return _Fail("expected ',' or '}'");
		}
	}

	const std::string&	fText;
	size_t				fPos = 0;
	JsonError			fError;
};


void
WriteString(std::string& out, const std::string& s)
{
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if (c < 0x20) {
					char buffer[8];
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
					out += buffer;
				} else
					out += char(c);
		}
	}
	out += '"';
}


void
WriteNumber(std::string& out, double n)
{
	if (!std::isfinite(n)) {
		out += "null";
		return;
	}
	if (n == std::floor(n) && std::fabs(n) < 1e15) {
		out += std::to_string(int64_t(n));
		return;
	}
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.10g", n);
	out += buffer;
}


void
WriteIndent(std::string& out, const JsonWriteOptions& options, int level)
{
	if (!options.pretty)
		return;
	out += '\n';
	if (options.indent <= 0)
		out.append(size_t(level), '\t');
	else
		out.append(size_t(level * options.indent), ' ');
}


void
WriteValue(std::string& out, const Value& v, const JsonWriteOptions& options,
	int level)
{
	switch (v.GetType()) {
		case Value::Type::Null: out += "null"; break;
		case Value::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
		case Value::Type::Number: WriteNumber(out, v.AsNumber()); break;
		case Value::Type::String: WriteString(out, v.AsString()); break;
		case Value::Type::Array:
		{
			if (v.Items().empty()) {
				out += "[]";
				break;
			}
			out += '[';
			bool first = true;
			for (const Value& item : v.Items()) {
				if (!first)
					out += ',';
				first = false;
				WriteIndent(out, options, level + 1);
				WriteValue(out, item, options, level + 1);
			}
			WriteIndent(out, options, level);
			out += ']';
			break;
		}
		case Value::Type::Object:
		{
			if (v.Members().empty()) {
				out += "{}";
				break;
			}
			out += '{';
			bool first = true;
			for (const Value::Member& m : v.Members()) {
				if (!first)
					out += ',';
				first = false;
				WriteIndent(out, options, level + 1);
				WriteString(out, m.first);
				out += options.pretty ? ": " : ":";
				WriteValue(out, m.second, options, level + 1);
			}
			WriteIndent(out, options, level);
			out += '}';
			break;
		}
	}
}

}	// namespace


bool
ParseJson(const std::string& text, Value& out, JsonError* error)
{
	Parser parser(text);
	Value result;
	if (!parser.Parse(result, error))
		return false;
	out = std::move(result);
	return true;
}


std::string
WriteJson(const Value& value, const JsonWriteOptions& options)
{
	std::string out;
	WriteValue(out, value, options, 0);
	if (options.pretty)
		out += '\n';
	return out;
}

}	// namespace lumina
