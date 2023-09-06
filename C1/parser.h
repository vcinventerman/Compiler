#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

#include <array>
#include <string_view>
#include <functional>
#include <algorithm>
#include <charconv>

#include "util.h"
#include "token.h"
#include "ast.h"

struct Parser {
	static constexpr const std::array<uint32_t, 3> WHITESPACE = { ' ', '\n', '\t' };

	// Begin and end sentinels
	std::string_view code;

	// Read cursor
	std::string_view::const_iterator r;

	SourcePos templatePos;

	Token currentTok;

	Parser(std::string_view _code, SourcePos _templatePos) : code(_code), r(code.cbegin()), templatePos(_templatePos) {}

	std::string_view::const_iterator skipws(std::string_view::const_iterator r) {
		if (r == code.end()) { return r; }

		while (isAnyOf(*r, WHITESPACE)) {
			r++;

			if (r == code.end()) { return r; }
		}

		return r;
	}

	// Returns: the state of r if this token is kept as-is
	std::string_view::const_iterator consume(std::string_view::const_iterator r, std::string_view& slot) {
		slot = "";

		r = skipws(r);
		if (r == code.end()) {
			return r;
		}

		slot = std::string_view(&*r, 1);
		r++;

		if (r == code.end()) {
			return r;
		}

		std::function<bool(uint32_t)> check;

		// Decide whether to consume more word tokens or more operator tokens (+=)
		if (isAnyOf(slot[0], OPERATOR_CHARACTERS)) {
			check = [&](uint32_t c) { return isAnyOf(*r, OPERATOR_CHARACTERS); };
		}
		else {
			check = [&](uint32_t c) { return !isAnyOf(c, WHITESPACE) && !isAnyOf(*r, OPERATOR_CHARACTERS); };
		}

		while (check(*r)) {
			r++;
			if (r == code.end()) { return r; }
			slot = std::string_view(slot.data(), &*r);
		}

		return r;
	}

	bool scanFloatLiteral(std::string_view wholeStr) {
		std::string_view period, decimal;

		auto r1 = consume(r, period);
		auto r2 = consume(r1, decimal);

		if (period == "." && std::all_of(decimal.cbegin(), decimal.cend(), [](char c) { return std::isdigit(c); })) {
			double num = 0.0;
			auto res = std::from_chars(wholeStr.data(), decimal.data() + decimal.size(), num);

			if (res.ec == std::errc{}) {
				r = r2;
				currentTok.defaultValue = num;
				return true;
			}
		}
		else {
			int num = 0;
			auto res = std::from_chars(wholeStr.data(), wholeStr.data() + wholeStr.size(), num);

			if (res.ec == std::errc{}) {
				currentTok.defaultValue = num;
				return true;
			}
		}

		return false;
	}

	void scanToken() {
		std::string_view str;
		r = consume(r, str);

		currentTok = {};

		if (str == "") {
			currentTok.type = TokenType::END_OF_FIELD;
			return;
		}

		currentTok.code = resolveSourcePos(templatePos.filename, code, str);

		auto operatorIndex = std::find(std::begin(OPERATOR_TOKENS), std::end(OPERATOR_TOKENS), str);
		if (operatorIndex != std::end(OPERATOR_TOKENS)) {
			currentTok.type = (TokenType)(operatorIndex - OPERATOR_TOKENS);
			return;
		}

		if (std::all_of(str.cbegin(), str.cend(), [](char c) { return std::isdigit(c); })) {
			// A token consisting entirely of digits is an integer or the first half of a float
			auto val = scanFloatLiteral(str);
			currentTok.type = TokenType::PRIMITIVE;
			return;
		}

		//todo: Hex
	}

	std::unique_ptr<AstNode> parseTerminalNode() {
		// Creates an AstNode from the current token, assuming it is a value
		if (currentTok.type == TokenType::PRIMITIVE) {
			auto out = AstNode::makeLeaf(currentTok);
			scanToken();
			return out;
		}
	}

	std::unique_ptr<AstNode> parseBinaryExpression(int previousTokenPrecedence) {
		std::unique_ptr<AstNode> left = parseTerminalNode();
		std::unique_ptr<AstNode> right;
		TokenType nodeType = currentTok.type;

		if (currentTok.type == TokenType::END_OF_FIELD) {
			return left;
		}

		while (OPERATOR_PRECEDENCE[(int)nodeType] > previousTokenPrecedence) {
			scanToken();

			int precedence = OPERATOR_PRECEDENCE[(int)nodeType];
			right = parseBinaryExpression(precedence);

			Token parent;
			parent.type = nodeType;
			left = AstNode::makeParent(parent, std::move(left), std::move(right));

			nodeType = currentTok.type;
			if (nodeType == TokenType::END_OF_FIELD) {
				break;
			}
		}

		return left;
	}

	void parse() {
		scanToken();

		auto statement = parseBinaryExpression(0);

		int res = interpretAst(statement.get());

		std::cout << std::format("Code: {}\nInterpets to {}\n", code, res);
	}
};

#endif