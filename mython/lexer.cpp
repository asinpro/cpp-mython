#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <istream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <optional>
#include <limits>

using namespace std;

namespace parse {

namespace details {

bool IsSpecialChar(char c) {
    return c == '=' || c == '!' || c == '<' || c == '>';
}

string ReadLine(istream& input) {
    string line;

    while(getline(input, line)) {
        if (!line.empty()) {
            break;
        }
    }

    return line;
}

Token ReadCompareOperator(char c) {
    switch(c) {
        case '=':
            return token_type::Eq{};
        case '!':
            return token_type::NotEq{};
        case '<':
            return token_type::LessOrEq{};
        case '>':
        default:
            return token_type::GreaterOrEq{};
    }
}

Token ReadKeyword(string literal) {
    if (literal == "class"s) {
        return token_type::Class{};
    } else if (literal == "return"s) {
        return token_type::Return{};
    } else if (literal == "if"s) {
        return token_type::If{};
    } else if (literal == "else"s) {
        return token_type::Else{};
    } else if (literal == "def"s) {
        return token_type::Def{};
    } else if (literal == "print"s) {
        return token_type::Print{};
    } else if (literal == "or"s) {
        return token_type::Or{};
    } else if (literal == "None"s) {
        return token_type::None{};
    } else if (literal == "and"s) {
        return token_type::And{};
    } else if (literal == "not"s) {
        return token_type::Not{};
    } else if (literal == "True"s) {
        return token_type::True{};
    } else if (literal == "False"s) {
        return token_type::False{};
    }

    return token_type::Id{move(literal)};
}

string ReadLiteral(istream& input) {
    string literal;
    char c;

    while(input.get(c)) {
        if (isdigit(c) || isalpha(c) || c == '_') {
            literal.push_back(c);
        } else {
            input.putback(c);
            break;
        }
    }

    return literal;
}

token_type::Number ReadNumber(istream& input) {
    int number;
    input >> number;

    return {number};
}

token_type::Id ReadId(istream& input) {
    return {ReadLiteral(input)};
}

token_type::String ReadString(istream& input, char prefix) {
    string s;
    char c;

    while(input.get(c)) {
        if (c == prefix) {
            break;
        } else if (c == '\\') {
            input.get(c);

            switch(c) {
                case 'n':
                    s.push_back('\n');
                    break;
                case 't':
                    s.push_back('\t');
                    break;
                case 'r':
                    s.push_back('\r');
                    break;
                case '\\':
                    s.push_back('\\');
                    break;
                case '\'':
                    s.push_back('\'');
                    break;
                case '"':
                    s.push_back('"');
                    break;
            }
        } else {
            s.push_back(c);
        }
    }

    return {move(s)};
}

} // namespace details

bool operator==(const Token& lhs, const Token& rhs) {
    using namespace token_type;

    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (lhs.Is<Char>()) {
        return lhs.As<Char>().value == rhs.As<Char>().value;
    }
    if (lhs.Is<Number>()) {
        return lhs.As<Number>().value == rhs.As<Number>().value;
    }
    if (lhs.Is<String>()) {
        return lhs.As<String>().value == rhs.As<String>().value;
    }
    if (lhs.Is<Id>()) {
        return lhs.As<Id>().value == rhs.As<Id>().value;
    }
    return true;
}

bool operator!=(const Token& lhs, const Token& rhs) {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Token& rhs) {
    using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

    VALUED_OUTPUT(Number);
    VALUED_OUTPUT(Id);
    VALUED_OUTPUT(String);
    VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

    UNVALUED_OUTPUT(Class);
    UNVALUED_OUTPUT(Return);
    UNVALUED_OUTPUT(If);
    UNVALUED_OUTPUT(Else);
    UNVALUED_OUTPUT(Def);
    UNVALUED_OUTPUT(Newline);
    UNVALUED_OUTPUT(Print);
    UNVALUED_OUTPUT(Indent);
    UNVALUED_OUTPUT(Dedent);
    UNVALUED_OUTPUT(And);
    UNVALUED_OUTPUT(Or);
    UNVALUED_OUTPUT(Not);
    UNVALUED_OUTPUT(Eq);
    UNVALUED_OUTPUT(NotEq);
    UNVALUED_OUTPUT(LessOrEq);
    UNVALUED_OUTPUT(GreaterOrEq);
    UNVALUED_OUTPUT(None);
    UNVALUED_OUTPUT(True);
    UNVALUED_OUTPUT(False);
    UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

    return os << "Unknown token :("sv;
}

Lexer::Lexer(std::istream& input) :
    input_(input) {
    line_ = ReadNextLine(input_, context_);
    NextToken();
}

const Token& Lexer::CurrentToken() const {
    return current_token_;
}

Token Lexer::NextToken() {
    return current_token_ = ReadToken();
}

Token Lexer::ReadToken() {
    if (context_.new_indent > context_.indent) {
        ++context_.indent;
        return token_type::Indent{};
    } else if (context_.new_indent < context_.indent) {
        --context_.indent;
        return token_type::Dedent{};
    }

    char c;

    if (!line_.get(c)) {
        if (input_.eof()) {
            if (context_.indent > 0) {
                --context_.indent;
                context_.new_indent = 0;
                return token_type::Dedent{};
            }

            return token_type::Eof{};
        }

        line_ = ReadNextLine(input_, context_);
        return ReadToken();
    }

    if (c == '\n') {
        return token_type::Newline{};
    }

    while(c == ' ') {
        line_.get(c);
    }

    if (c == '#') {
        line_.ignore(numeric_limits<streamsize>::max(), '\n');
        return token_type::Newline{};
    }

    if (details::IsSpecialChar(c)) {
        if (line_.peek() == '=') {
            line_.get();
            return details::ReadCompareOperator(c);
        }
    }

    if (isalpha(c) || c == '_') {
        line_.putback(c);
        return details::ReadKeyword(details::ReadLiteral(line_));
    }

    if (c == '"' || c == '\'') {
        return details::ReadString(line_, c);
    }

    if (isdigit(c)) {
        line_.putback(c);
        return details::ReadNumber(line_);
    }

    return token_type::Char{c};
}

istringstream Lexer::ReadNextLine(std::istream& input, Context& context) {
    string line;

    while(getline(input, line)) {
        if (!line.empty()) {
            auto pos = line.find_first_not_of(' ');

            if (pos != string::npos && line[pos] != '#') {
                context.new_indent = pos / 2;
                line += '\n';
                return istringstream(move(line));
            }
        }
    }

    return istringstream();
}

}  // namespace parse
