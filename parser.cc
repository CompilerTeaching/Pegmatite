/*-
 * Copyright (c) 2012, Achilleas Margaritis
 * Copyright (c) 2014, David T. Chisnall
 * Copyright (c) 2016, Jonathan Anderson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef PEGMATITE_PLATFORM_UNIX
#include <sys/uio.h>
#include <unistd.h>
#endif

#include "parser.hh"


using namespace pegmatite;

namespace {
//parser state
// FIXME: This class has an uninformative name.
class ParsingState
{
public:
	//position
	ParserPosition position;

	//size of match vector
	size_t matches;

	//constructor
	ParsingState(Context &con);
};



//match
class ParseMatch
{
public:
	//const Rule matched
	const Rule *matched_rule;

	//source range
	InputRange source;

	//null constructor
	ParseMatch() {}

	//constructor from parameters
	ParseMatch(const Rule *r, const ParserPosition &b, const ParserPosition &e) :
		matched_rule(r),
		source(b, e)
	{
	}
};

/**
 * String expression.  Matches a sequence of characters.
 */
class StringExpr : public Expr
{
public:
	/**
	 * Returns a new string expression recognising the specified string.
	 */
	StringExpr(const char *s) : characters(s, s + strlen(s)) {}
	/**
	 * Returns a new string expression recognising the specified string.
	 */
	StringExpr(const char *s, std::size_t length) : characters(s, s + length) {}
	virtual bool parse_non_term(Context &con) const;
	virtual bool parse_term(Context &con) const;
	virtual void dump() const;
private:
	/**
	 * The characters that this expression will match.
	 */
	std::vector<char32_t> characters;
};


}
namespace pegmatite
{

void defaultErrorReporter(const InputRange& ir, const std::string& message)
{
	std::cerr
		<< "error at " << ir.start.line << ":" << ir.finish.col
		<< ": " << message << std::endl
		;
}

/**
 * Out-of-line virtual destructor forces vtable to be emitted in this
 * translation unit only.
 */
Expr::~Expr()
{
}

/**
 * Character expression, matches a single character.
 */
class CharacterExpr : public Expr
{
	/**
	 * The character that will be recognised by this expression.
	 */
	char32_t character;
public:
	/**
	 * Constructs a character expression from the specified integer.
	 */
	CharacterExpr(char32_t c) : character(c) {}
	virtual bool parse_non_term(Context &con) const;
	virtual bool parse_term(Context &con) const;
	virtual void dump() const;
	/**
	 * Returns a range expression that recognises characters in the specified
	 * range.
	 */
	ExprPtr operator-(const CharacterExpr &other);
	/**
	 * Returns a range expression that recognises characters in the specified
	 * range.
	 */
	ExprPtr operator-(char32_t other);
};

CharacterExprPtr operator "" _E(const char x)
{
	return CharacterExprPtr(new CharacterExpr(static_cast<char32_t>(x)));
}
CharacterExprPtr operator "" _E(const char32_t x)
{
	return CharacterExprPtr(new CharacterExpr(x));
}
ExprPtr operator "" _S(const char *x, std::size_t)
{
	return set(x);
}
ExprPtr operator "" _E(const char *x, std::size_t len)
{
	return ExprPtr(new StringExpr(x, len));
}
ExprPtr operator-(const CharacterExprPtr &left, const CharacterExprPtr &right)
{
	return (*left) - (*right);
}
ExprPtr operator-(const CharacterExprPtr &left, char32_t right)
{
	return (*left) - right;
}

ExprPtr::ExprPtr(const CharacterExprPtr &e) :
	std::shared_ptr<Expr>(std::static_pointer_cast<Expr>(e)) {}

//parsing context
class Context
{
public:
	//const Rule that parses whitespace
	const Rule &whitespace_rule;

	//current position
	ParserPosition position;

	//Error position
	ParserPosition error_pos;

	//input begin
	Input::iterator start;

	//input end
	Input::iterator finish;

	//matches
	std::vector<ParseMatch> matches;

	/**
	 * Depth of parsing.  Used for trace expressions.
	 */
	int depth = 0;

	const ParserDelegate &delegate;

	//constructor
	Context(Input &i, const Rule &ws, const ParserDelegate &d) :
		whitespace_rule(ws),
		position(i),
		error_pos(i),
		start(i.begin()),
		finish(i.end()),
		delegate(d)
	{
	}

	//check if the end is reached
	bool end() const
	{
		return position.it == finish;
	}

	//get the current symbol
	char32_t symbol() const
	{
		assert(!end());
		return *position.it;
	}

	//set the longest possible error
	void set_error_pos()
	{
		if (position.it > error_pos.it)
		{
			error_pos = position;
		}
	}

	//next column
	void next_col()
	{
		++position.it;
		++position.col;
	}

	/**
	 * Consume the specified number of characters in the input.
	 */
	void consume(size_t chars)
	{
		position.it += chars;
		position.col += static_cast<int>(chars);
	}

	//next line
	void next_line()
	{
		++position.line;
		position.col = 1;
	}

	//restore the state
	void restore(const ParsingState &st)
	{
		position = st.position;
		matches.resize(st.matches);
	}

	//parse non-term rule.
	bool parse_non_term(const Rule &r);

	//parse term rule.
	bool parse_term(const Rule &r);

	//parse whitespace terminal
	bool parse_ws() { return parse_term(whitespace_rule); }

	parse_proc get_parse_proc(const Rule &r) const
	{
		return delegate.get_parse_proc(r);
	}

	//execute all the parse procs
	bool do_parse_procs(void *d) const
	{
		for(const auto &m : matches)
		{
			const parse_proc &p = get_parse_proc(*(m.matched_rule));
			assert(p);
			if (!p(m.source, d))
				return false;
		}

		return true;
	}

	/**
	 * Empty the cache.
	 */
	void clear_cache() { cache.clear(); }

private:
	/**
	 * The mode for parsing a rule.
	 */
	enum class MatchMode
	{
		/**
		 * Parse as normal.  If we're left recursing, then try again in reject
		 * mode.
		 */
		PARSE,
		/**
		 * Parse as normal only if we are not left recursing.  If we are, then
		 * fail this rule to force backtracking.
		 */
		REJECT
	};

	//state
	struct RuleState
	{
		//position in source code, relative to start
		size_t position;

		//mode
		MatchMode mode;

		//constructor
		RuleState(size_t ParserPosition = Input::npos, MatchMode m = MatchMode::PARSE) :
			position(ParserPosition), mode(m) {}
	};
	//parse non-term rule.
	//parse term rule.
	std::unordered_map<const Rule*, std::vector<RuleState>> rule_states;
	bool parse_rule(const Rule &r, bool (Context::*parse_func)(const Rule &));
	bool _parse_non_term(const Rule &r);

	bool _parse_term(const Rule &r);

	/**
	 * The key used in the parsing cache.
	 */
	struct CacheKey
	{
		/**
		 * The rule for which this cache entry applies.
		 */
		const Rule      *rule;
		/**
		 * The start address for this rule.
		 */
		Input::iterator  start;
		/**
		 * Compare two keys for equality.
		 */
		bool operator==(const CacheKey &other) const
		{
			return (rule == other.rule) && (start == other.start);
		}
	};
	/**
	 * The hash calculator for CacheKey objects.  Note that performance of the
	 * parser is *highly* dependent on the quality of this hash function.
	 */
	struct CacheKeyHash
	{
		/**
		 * The hash function, xors the hash of the rule and the start index
		 * together.
		 */
		size_t operator()(const CacheKey &k) const
		{
			std::hash<const Rule*> h;
			std::hash<Input::Index> hi;
			return h(k.rule) ^ (hi(k.start.index() << 1));
		}
	};
	/**
	 * The type for cached entries.  The cache contains the position after
	 * parsing a rule and the list of rules that were matched.
	 */
	typedef std::pair<ParserPosition, std::vector<ParseMatch>> CacheEntry;
	/*
	 * The cache.  After each rule is parsed, we cache the result to avoid
	 * recomputing.  Note that we currently do not cache parse failures.
	 */
	std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache;
};

}

namespace{

/**
 * Set expression: matches characters in a set.
 */
class SetExpr : public Expr
{
public:
	//constructor from ansi string.
	SetExpr(const char *s)
	{
		for(; *s; ++s)
		{
			_add(static_cast<char32_t>(*s));
		}
	}

	//constructor from range.
	SetExpr(char32_t min, char32_t max)
	{
		assert(min >= 0);
		assert(min <= max);
		mSetExpr.resize(static_cast<size_t>(max) + 1U);
		for(; min <= max; ++min)
		{
			mSetExpr[static_cast<size_t>(min)] = true;
		}
	}

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		return _parse(con);
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		return _parse(con);
	}

	virtual void dump() const
	{
		fprintf(stderr, "[");
		char c = 0;
		for (bool v : mSetExpr)
		{
			if (v)
				fprintf(stderr, "%c", c);
			c++;
		}
		fprintf(stderr, "]");
	}

private:
	//set is kept as an array of flags, for quick access
	std::vector<bool> mSetExpr;

	//add character
	void _add(char32_t i)
	{
		if (i >= mSetExpr.size())
		{
			mSetExpr.resize(i + 1);
		}
		mSetExpr[i] = true;
	}

	//internal parse
	bool _parse(Context &con) const
	{
		if (!con.end())
		{
			size_t ch = con.symbol();
			if (ch < mSetExpr.size() && mSetExpr[ch])
			{
				con.next_col();
				return true;
			}
		}
		con.set_error_pos();
		return false;
	}
};

/**
 * Adaptor that modifies the type of the iterator.  This lets us use iterators
 * with things that expect char or whcar_t iterators (as long as casts work
 * appropriately).
 */
template<typename Src, typename Out, typename In>
class IteratorAdaptor
{
		Src s;
		public:

		typedef std::bidirectional_iterator_tag iterator_category;
		typedef std::ptrdiff_t difference_type;
		typedef Out value_type;
		typedef const Out& reference;
		typedef const Out* pointer;

		inline IteratorAdaptor(Src src) : s(src) {}
		inline IteratorAdaptor() {}
		inline Out operator*() const { return static_cast<Out>(*s); }
		inline IteratorAdaptor &operator++()
		{
			++s;
			return *this;
		}
		inline IteratorAdaptor<Src, Out,In> operator++(int /*dummy*/)
		{
			auto copy = *this;
			s++;
			return copy;
		}
		inline IteratorAdaptor<Src, Out,In> &operator--()
		{
			--s;
			return *this;
		}
		inline bool operator==(const IteratorAdaptor<Src, Out,In> &other) const
		{
			return s == other.s;
		}
		inline bool operator!=(const IteratorAdaptor<Src, Out,In> &other) const
		{
			return s != other.s;
		}
		inline bool operator>(const IteratorAdaptor<Src, Out,In> &other) const
		{
			return s > other.s;
		}
		inline bool operator<(const IteratorAdaptor<Src, Out,In> &other) const
		{
			return s < other.s;
		}
};

/**
 * Preform a regular expression match, starting at `begin` and trying to match
 * up to `end`.  Returns true if the regular expression matches the input,
 * false otherwise.  If there is a match, then `length` will be set to the
 * length of the match.
 */
template <typename T>
bool regexMatch(Input::iterator begin,
                Input::iterator end,
                const std::basic_regex<T> &r,
                size_t &length)
{
	typedef IteratorAdaptor<Input::iterator, T, char32_t> Iterator;
	std::match_results<Iterator> match;
	Iterator b(begin), e(end);
	if (std::regex_search(b, e, match, r, std::regex_constants::match_continuous))
	{
		length = static_cast<size_t>(match.length());
		return true;
	}
	return false;
}

/**
 * Matches characters that correspond to a given regular expression.
 */
template<typename CharTy>
class RegexExpr : public Expr
{
	std::basic_regex<CharTy> r;
	bool parse(Context &con) const
	{
		size_t length;
		if (regexMatch(con.position.it, con.finish, r, length))
		{
			con.consume(length);
			return true;
		}
		con.set_error_pos();
		return false;
	}
public:
	RegexExpr(const CharTy *s) : r(s, std::regex_constants::optimize) {}
	RegexExpr(const CharTy *s, size_t count) : r(s, count, std::regex_constants::optimize) {}

	virtual bool parse_non_term(Context &con) const
	{
		return parse(con);
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		return parse(con);
	}

	virtual void dump() const
	{
		fprintf(stderr, "<regex>");
	}
};


//base class for unary expressions
class UnaryExpr : public Expr
{
public:
	UnaryExpr(const ExprPtr e) : expr(e) { }
protected:
	const ExprPtr expr;
};


//terminal
class TerminalExpr : public UnaryExpr
{
public:
	//constructor.
	TerminalExpr(const ExprPtr e) :
		UnaryExpr(e)
	{
	}

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		return expr->parse_term(con);
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		return expr->parse_term(con);
	}

	virtual void dump() const
	{
		expr->dump();
	}

};


//loop 0
class Loop0Expr : public UnaryExpr
{
public:
	//constructor.
	Loop0Expr(const ExprPtr e) :
		UnaryExpr(e)
	{
	}

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		//if parsing of the first fails, restore the context and stop
		con.parse_ws();
		ParsingState st(con);
		if (!expr->parse_non_term(con))
		{
			con.restore(st);
			return true;
		}

		//parse the rest
		for(;;)
		{
			con.parse_ws();
			ParsingState s(con);
			if (!expr->parse_non_term(con))
			{
				con.restore(s);
				break;
			}
		}

		return true;
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		//if parsing of the first fails, restore the context and stop
		ParsingState st(con);
		if (!expr->parse_term(con))
		{
			con.restore(st);
			return true;
		}

		//parse the rest until no more parsing is possible
		for(;;)
		{
			ParsingState s(con);
			if (!expr->parse_term(con))
			{
				con.restore(s);
				break;
			}
		}

		return true;
	}
	virtual void dump() const
	{
		fprintf(stderr, "*( ");
		expr->dump();
		fprintf(stderr, " )");
	}
};


//loop 1
class Loop1Expr : public UnaryExpr
{
public:
	Loop1Expr(const ExprPtr e) : UnaryExpr(e) { }

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		//parse the first; if the first fails, stop
		con.parse_ws();
		if (!expr->parse_non_term(con)) return false;

		//parse the rest until no more parsing is possible
		for(;;)
		{
			con.parse_ws();
			ParsingState st(con);
			if (!expr->parse_non_term(con))
			{
				con.restore(st);
				break;
			}
		}

		return true;
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		//parse the first; if the first fails, stop
		if (!expr->parse_term(con)) return false;

		//parse the rest until no more parsing is possible
		for(;;)
		{
			ParsingState st(con);
			if (!expr->parse_term(con))
			{
				con.restore(st);
				break;
			}
		}

		return true;
	}

	virtual void dump() const
	{
		fprintf(stderr, "+( ");
		expr->dump();
		fprintf(stderr, " )");
	}
};


//optional
class OptionalExpr : public UnaryExpr
{
public:
	OptionalExpr(const ExprPtr e) : UnaryExpr(e) { }

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		ParsingState st(con);
		if (!expr->parse_non_term(con)) con.restore(st);
		return true;
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		ParsingState st(con);
		if (!expr->parse_term(con)) con.restore(st);
		return true;
	}

	virtual void dump() const
	{
		fprintf(stderr, "-( ");
		expr->dump();
		fprintf(stderr, " )");
	}
};


//and
//FIXME: What is this?  Parses the subexpression but resets the state
//independent of success or failure?
//THIS IS WHY COMMENTS MATTER!
class AndExpr : public UnaryExpr
{
public:
	AndExpr(const ExprPtr e) : UnaryExpr(e) { }

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		ParsingState st(con);
		bool ok = expr->parse_non_term(con);
		con.restore(st);
		return ok;
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		ParsingState st(con);
		bool ok = expr->parse_term(con);
		con.restore(st);
		return ok;
	}

	virtual void dump() const
	{
		fprintf(stderr, "&( ");
		expr->dump();
		fprintf(stderr, " )");
	}
};


//not
class NotExpr : public UnaryExpr
{
public:
	NotExpr(const ExprPtr e) : UnaryExpr(e) { }

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		ParsingState st(con);
		bool ok = !expr->parse_non_term(con);
		con.restore(st);
		return ok;
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		ParsingState st(con);
		bool ok = !expr->parse_term(con);
		con.restore(st);
		return ok;
	}

	virtual void dump() const
	{
		fprintf(stderr, "!( ");
		expr->dump();
		fprintf(stderr, " )");
	}
};


//newline
class NewlineExpr : public UnaryExpr
{
public:
	NewlineExpr(const ExprPtr e) : UnaryExpr(e) { }

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		if (!expr->parse_non_term(con)) return false;
		con.next_line();
		return true;
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		if (!expr->parse_term(con)) return false;
		con.next_line();
		return true;
	}

	virtual void dump() const
	{
		fprintf(stderr, "nl( ");
		expr->dump();
		fprintf(stderr, " )");
	}
};


//base class for binary expressions
class BinaryExpr : public Expr
{
public:
	BinaryExpr(const ExprPtr &l, const ExprPtr &r) :
		left(l), right(r) { }
protected:
	const ExprPtr left, right;
};


//sequence
class SequenceExpr : public BinaryExpr
{
public:
	SequenceExpr(const ExprPtr l, const ExprPtr r) : BinaryExpr(l, r) {}

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		if (!left->parse_non_term(con)) return false;
		con.parse_ws();
		return right->parse_non_term(con);
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		if (!left->parse_term(con)) return false;
		return right->parse_term(con);
	}

	virtual void dump() const
	{
		left->dump();
		fprintf(stderr, " >> ");
		right->dump();
	}
};


//choice
class ChoiceExpr : public BinaryExpr
{
public:
	ChoiceExpr(const ExprPtr &l, const ExprPtr &r) :
		BinaryExpr(l, r) {}

	virtual bool parse_non_term(Context &con) const
	{
		ParsingState st(con);
		if (left->parse_non_term(con)) return true;
		con.restore(st);
		return right->parse_non_term(con);
	}

	virtual bool parse_term(Context &con) const
	{
		ParsingState st(con);
		if (left->parse_term(con)) return true;
		con.restore(st);
		return right->parse_term(con);
	}

	virtual void dump() const
	{
		left->dump();
		fprintf(stderr, " | ");
		right->dump();
	}
};


//reference to rule
class RuleReferenceExpr : public Expr
{
public:
	//constructor.
	RuleReferenceExpr(const Rule &r) : referenced_rule(r) { }

	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		return con.parse_non_term(referenced_rule);
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		return con.parse_term(referenced_rule);
	}

	virtual void dump() const
	{
		fprintf(stderr, "{Reference to rule}");
	}

private:
	//reference
	const Rule &referenced_rule;
};


//eof
class EndOfFileExpr : public Expr
{
public:
	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		return parse_term(con);
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		return con.end();
	}

	virtual void dump() const
	{
		fprintf(stderr, "$eof");
	}
};


//AnyExpr
class AnyExpr : public Expr
{
public:
	//parse with whitespace
	virtual bool parse_non_term(Context &con) const
	{
		return parse_term(con);
	}

	//parse terminal
	virtual bool parse_term(Context &con) const
	{
		if (!con.end())
		{
			con.next_col();
			return true;
		}
		con.set_error_pos();
		return false;
	}

	virtual void dump() const
	{
		fprintf(stderr, "$AnyExpr");
	}
};
/**
 * Trace expressions have no effect on parsing.  They wrap another expression
 * and log a message when parsing for it begins and ends, along with whether it
 * succeeds.
 */
class TraceExpr : public Expr
{
	/**
	 * The message to log for this expression.
	 */
	const char *message;
	/**
	 * The real expression that will handle the parsing.
	 */
	const ExprPtr expr = static_cast<Expr*>(nullptr);
	void log(int depth, const char *event, const char *result, Context &con) const
	{
		fprintf(stderr, "[%d] ", depth);
		for (int i=0 ; i<depth ; i++)
		{
			fprintf(stderr, " ");
		}
		fprintf(stderr, "%s %s (line %d, column %d)\n",
		                event,
		                result,
		                con.position.line,
		                con.position.col);
	}
public:
#ifdef DEBUG_PARSING
	TraceExpr(const char *m, const ExprPtr e) : message(m), expr(e) {}
#endif

	virtual bool parse_non_term(Context &con) const
	{
		log(con.depth++, "Trying", message, con);
		bool result = expr->parse_non_term(con);
		log(--con.depth, message, result ? "succeeded" : "failed", con);
		return result;
	}
	virtual bool parse_term(Context &con) const
	{
		log(con.depth++, "Trying", message, con);
		bool result = expr->parse_term(con);
		log(--con.depth, message, result ? "succeeded" : "failed", con);
		return result;
	}

	virtual void dump() const
	{
		expr->dump();
	}
};
class DebugExpr : public Expr
{
	std::function<void()> fn;
public:
	DebugExpr(std::function<void()> f) : fn(f) {}

	//parse with whitespace
	virtual bool parse_non_term(Context &) const
	{
		fn();
		return true;
	}

	//parse terminal
	virtual bool parse_term(Context &) const
	{
		fn();
		return true;
	}

	virtual void dump() const
	{
		fprintf(stderr, "<DEBUG: ");
		fn();
		fprintf(stderr, ">");
	}
};

//constructor
ParsingState::ParsingState(Context &con) :
	position(con.position),
	matches(con.matches.size())
{
}

static inline bool parseString(Context &con,
                               const std::vector<char32_t> &characters)
{
	for (char32_t c : characters)
	{
		if (con.end() || con.symbol() != c)
		{
			con.set_error_pos();
			return false;
		}
		con.next_col();
	}
	return true;
}
bool StringExpr::parse_non_term(Context &con) const
{
	return parseString(con, characters);
}
bool StringExpr::parse_term(Context &con) const
{
	return parseString(con, characters);
}
void StringExpr::dump() const
{
	fprintf(stderr, "\"");
	for (char32_t c : characters)
	{
		fprintf(stderr, "%c", static_cast<char>(c));
	}
	fprintf(stderr, "\"");
}



}

namespace pegmatite {


//parse non-term rule.
bool Context::parse_non_term(const Rule &r)
{
	return parse_rule(r, &Context::_parse_non_term);
}

#ifdef _MSC_VER
// Disable uninit variable check as MSVC is not detecting exhaustive enum usage.
#  pragma warning(push)
#  pragma warning(disable: 4701)
#endif
bool Context::parse_rule(const Rule &r, bool (Context::*parse_func)(const Rule &))
{
	// For each rule, we maintain a vector consisting of where it was last
	// encountered (in the input stream) and what the parsing mode was.
	auto &states = rule_states[std::addressof(r)];
	// If this is the first time that we've encountered this rule, then set the
	// last position and mode to values that will trigger a normal parse: We
	// can't be in left recursion if this is the first time that we've
	// encountered the rule.
	size_t last_pos = Input::npos;
	MatchMode last_mode = MatchMode::PARSE;
	if (!states.empty())
	{
		auto &last = states.back();
		last_pos = last.position;
		last_mode = last.mode;
	}
	// Return value (success or failure of parse)
	bool ok;

	// Compute the new position in the stream.  We're only tracking offsets to
	// detect left recursion, not storing iterators.
	size_t new_pos = position.it - start;

	// Check if we have left recursion.  We are in a left-recursive state if
	// the last time that we encountered this rule was at the same point in the
	// input.
	bool lr = new_pos == last_pos;

	// Look up the current rule and parser position in the cache to see if
	// we've been here before and successfully parsed the rule.
	CacheKey k = { std::addressof(r), position.it };
	auto cache_entry = cache.find(k);
	if (cache_entry != cache.end())
	{
		// If we have a cache entry then grab the list of matched rules and the
		// end parsing position from the cache and don't bother trying to apply
		// the rules again.
		auto cached_matches = cache_entry->second.second;
		matches.insert(matches.end(), cached_matches.begin(), cached_matches.end());
		position = cache_entry->second.first;
		return true;
	}

	size_t new_match_index = matches.size();

	switch (last_mode)
	{
		//normal parse
		case MatchMode::PARSE:
			if (lr)
			{
				//first try to parse the rule by rejecting it, so alternative
				//branches are examined
				states.push_back(RuleState(new_pos, MatchMode::REJECT));
				ok = (this->*parse_func)(r);
				states.pop_back();
				break;
			}
			else
			{
				states.push_back(RuleState(new_pos, MatchMode::PARSE));
				ok = (this->*parse_func)(r);
				states.pop_back();
			}
			break;
		case MatchMode::REJECT:
			if (lr)
			{
				ok = false;
			}
			else
			{
				states.push_back(RuleState(new_pos, MatchMode::PARSE));
				ok = (this->*parse_func)(r);
				states.pop_back();
			}
			break;
	}

	// If we successfully parsed the input, then cache the result.
	if (ok)
	{
		// To prevent the cache growing too large, if it starts to get quite
		// big, delete everything.  256is a mostly arbitrary number generated
		// by running a big(ish) parse with a few different values and finding
		// the place where the increase in memory didn't come with a noticeable
		// speedup.
		if (cache.size() > 256)
		{
			cache.clear();
		}
		// Insert the new cache entry
		auto &new_cache = cache[k];
		new_cache.first = position;
		auto &cached_matches = new_cache.second;
		cached_matches.clear();
		// If there some rules were matched, record them
		if (matches.size() > new_match_index)
		{
			const auto index =
				static_cast<Input::iterator::difference_type>(new_match_index);
			cached_matches.insert(cached_matches.begin(), matches.begin() +
					index, matches.end());
		}
	}

	return ok;
}
#ifdef _MSC_VER
#  pragma warning(pop)
#endif


//parse term rule.
bool Context::parse_term(const Rule &r) 
{
	return parse_rule(r, &Context::_parse_term);
}
const bool debug_parsing = false;


//parse non-term rule internal.
bool Context::_parse_non_term(const Rule &r)
{
	bool ok;
	if (get_parse_proc(r))
	{
		ParserPosition b = position;
		ok = r.expr->parse_non_term(*this);
		if (debug_parsing)
		{
			r.expr->dump();
			fprintf(stderr, "\n");
		}
		if (ok)
		{
			matches.push_back(ParseMatch(std::addressof(r), b, position));
		}
	}
	else
	{
		ok = r.expr->parse_non_term(*this);
	}
	return ok;
}


//parse term rule internal.
bool Context::_parse_term(const Rule &r)
{
	bool ok;
	if (get_parse_proc(r))
	{
		ParserPosition b = position;
		ok = r.expr->parse_term(*this);
		if (ok)
		{
			matches.push_back(ParseMatch(std::addressof(r), b, position));
		}
	}
	else
	{
		ok = r.expr->parse_term(*this);
	}
	return ok;
}


//get the next position
static ParserPosition _next_pos(const ParserPosition &p)
{
	ParserPosition r = p;
	++r.it;
	++r.col;
	return r;
}


//get syntax error
static void _syntax_Error(ErrorReporter &err, Context &con)
{
	err(InputRange(con.error_pos, _next_pos(con.error_pos)), "syntax error");
}


//get eof error
static void _eof_Error(ErrorReporter &err, Context &con)
{
	err(InputRange(con.error_pos, con.error_pos), "EOF");
}

char32_t Input::slowCharacterLookup(Index n)
{
	const int back_seek = static_buffer_size / 4;
	if (n >= size())
	{
		return 0;
	}
	// Optimise backtracking by jumping back 64 characters so subsequent
	// forward scans are fast.
	if (n < buffer_start)
	{
		buffer_start = (n > back_seek) ? n - back_seek : 0;
	}
	else
	{
		buffer_start = n;
	}
	Index length = static_buffer_size;
	buffer = local_buffer;
	if (!fillBuffer(buffer_start, length, buffer))
	{
		buffer_end = 0;
		return 0;
	}
	buffer_end = buffer_start + length;
	assert((n >= buffer_start) && (n < buffer_end));
	return buffer[n - buffer_start];
}
Input::~Input() {}

Input::Input(const Input& orig)
	: user_name(orig.user_name), buffer(orig.buffer),
	  buffer_start(orig.buffer_start), buffer_end(orig.buffer_end) {}

const std::string& Input::iterator::filename() const
{
	static std::string& None = *new std::string("<invalid input>");
	return buffer ? buffer->name() : None;
}

bool  UnicodeVectorInput::fillBuffer(Index start, Index &length, char32_t *&b)
{
	if (start > vector.size())
	{
		return false;
	}
	length = vector.size() - start;
	// The buffer can't be const because this function is allowed to write into
	// it, but the caller guarantees that it will not write to the returned
	// value.
	// We should probably clean up this interface to a const char32_t* instead
	// of a bool...
	b = const_cast<char32_t*>(vector.data() + start);
	return true;
}
Input::Index UnicodeVectorInput::size() const
{
	return vector.size();
}

bool  StringInput::fillBuffer(Index start, Index &length, char32_t *&b)
{
	if (start > str.size())
	{
		return false;
	}
	length = std::min(length, str.size() - start);
	std::size_t j = 0;
	for (Index i = start; i < start + length; ++i, ++j)
	{
		b[j] = static_cast<char32_t>(str[i]);
	}
	return true;
}
Input::Index StringInput::size() const
{
	return str.size();
}

const std::size_t Input::static_buffer_size;

#ifdef PEGMATITE_PLATFORM_UNIX
AsciiFileInput::AsciiFileInput(int file, const std::string& name)
	: Input(name), fd(file)
{
	struct stat buf;
	if (fstat(fd, &buf) != 0)
	{
		file_size = 0;
		perror("Input error");
	}
	else
	{
		file_size = static_cast<Index>(buf.st_size);
	}
}

bool AsciiFileInput::fillBuffer(Index start, Index &length, char32_t *&b)
{
	if (start > file_size)
	{
		return false;
	}
	char buffer[static_buffer_size];
	// This should be a no-op
	length = std::min(length, static_buffer_size);
	length = std::min(length, file_size - start);
	Index bytes_to_read = length;
	do {
		ssize_t ret = pread(fd, buffer, length,
		                    static_cast<off_t>(start));
		if (ret < 1)
		{
			return false;
		}
		bytes_to_read -= length;
	} while (bytes_to_read > 0);
	for (Index i=0 ; i<length ; i++)
	{
		b[i] = static_cast<char32_t>(buffer[i]);
	}
	return true;
}
Input::Index AsciiFileInput::size() const
{
	return file_size;
}
#endif

StreamInput StreamInput::Create(const std::string& name, std::istream& s)
{
	const std::streamoff start = s.tellg();
	s.seekg(0, std::ios::end);

	const std::streamoff len = s.tellg() - start;
	s.seekg(start, std::ios::beg);

	return StreamInput(name, s, static_cast<size_t>(len));
}

StreamInput::StreamInput(const std::string& name, std::istream& s, size_t len)
	: Input(name), length(len), stream(s)
{
}

bool StreamInput::fillBuffer(Index start, Index &len, char32_t *&b)
{
	if (start > length)
	{
		return false;
	}

	char l_buffer[static_buffer_size];
	len = std::min(this->length, static_buffer_size);
	len = std::min(len, this->length - start);

	stream.seekg(static_cast<std::streamoff>(start));
	if (!stream.good())
	{
		return false;
	}

	stream.read(l_buffer, static_cast<std::streamsize>(len));
	if (static_cast<Index>(stream.gcount()) != len)
	{
		return false;
	}

	for (Index i = 0 ; i < len; i++)
	{
		b[i] = static_cast<char32_t>(l_buffer[i]);
	}

	return true;
}

Input::Index StreamInput::size() const
{
	return length;
}

/** constructor from input.
	@param i input.
 */
ParserPosition::ParserPosition(Input &i) :
	it(i.begin()),
	line(1),
	col(1)
{
}

/** creates a zero-or-more loop out of this expression.
	@return a zero-or-more loop expression.
 */
ExprPtr operator *(const ExprPtr &e)
{
	return ExprPtr(new Loop0Expr(e));
}


/** creates a one-or-more loop out of this expression.
	@return a one-or-more loop expression.
 */
ExprPtr operator +(const ExprPtr &e)
{
	return ExprPtr(new Loop1Expr(e));
}


/** creates an optional out of e expression.
	@return an optional expression.
 */
ExprPtr operator -(const ExprPtr &e)
{
	return ExprPtr(new OptionalExpr(e));
}


/** creates an AND-expression.
	@return an AND-expression.
 */
ExprPtr operator &(const ExprPtr &e)
{
	return ExprPtr(new AndExpr(e));
}


/** creates a NOT-expression.
	@return a NOT-expression.
 */
ExprPtr operator !(const ExprPtr &e)
{
	return ExprPtr(new NotExpr(e));
}


/** constructor.
	@param b begin position.
	@param e end position.
 */
InputRange::InputRange(const ParserPosition &b, const ParserPosition &e) : start(b), finish(e) { }

std::string InputRange::str() const
{
	std::stringstream s;

	for (char32_t c : *this)
	{
		s << static_cast<char>(c);
	}

	return s.str();
}


Rule::Rule(const ExprPtr e) :
	expr(e)
{
}

Rule& Rule::operator=(Rule &&r)
{
	expr = std::move(r.expr);
	return *this;
}

/** constructor from rule.
	@param r rule.
 */
ExprPtr::ExprPtr(const char *s) : std::shared_ptr<Expr>(new StringExpr(s)) {}
ExprPtr::ExprPtr(const char32_t s) : std::shared_ptr<Expr>(new CharacterExpr(s)) {}
ExprPtr::ExprPtr(const Rule &r) : std::shared_ptr<Expr>(new RuleReferenceExpr(r)) {}


/** creates a sequence of expressions.
	@param left left operand.
	@param right right operand.
	@return an expression which parses a sequence.
 */
ExprPtr operator >> (const ExprPtr &left, const ExprPtr &right)
{
	return ExprPtr(new SequenceExpr(left, right));
}


/** creates a choice of expressions.
	@param left left operand.
	@param right right operand.
	@return an expression which parses a choice.
 */
ExprPtr operator | (const ExprPtr &left, const ExprPtr &right)
{
	return ExprPtr(new ChoiceExpr(left, right));
}


/** converts a parser expression into a terminal.
	@param e expression.
	@return an expression which parses a terminal.
 */
ExprPtr term(const ExprPtr &e)
{
	return ExprPtr(new TerminalExpr(e));
}


/** creates a set expression from a null-terminated string.
	@param s null-terminated string with characters of the set.
	@return an expression which parses a single character out of a set.
 */
ExprPtr set(const char *s)
{
	return ExprPtr(new SetExpr(s));
}

/**
 * Creates a regex expression from the specified string.
 */
ExprPtr regex(const char *s)
{
	return ExprPtr(new RegexExpr<char>(s));
}

/**
 * Creates a regex expression from the specified wide string.
 */
ExprPtr regex(const wchar_t *s)
{
	return ExprPtr(new RegexExpr<wchar_t>(s));
}
ExprPtr operator "" _R(const char *x, std::size_t len)
{
	return ExprPtr(new RegexExpr<char>(x, len));
}


/** creates a range expression.
	@param min min character.
	@param max max character.
	@return an expression which parses a single character out of range.
 */
ExprPtr range(char32_t min, char32_t max)
{
	return ExprPtr(new SetExpr(min, max));
}


/** creates an expression which increments the line counter
	and resets the column counter when the given expression
	is parsed successfully; used for newline characters.
	@param e expression to wrap into a newline parser.
	@return an expression that handles newlines.
 */
ExprPtr nl(const ExprPtr &e)
{
	return ExprPtr(new NewlineExpr(e));
}


/** creates an expression which tests for the end of input.
	@return an expression that handles the end of input.
 */
ExprPtr eof()
{
	return ExprPtr(new EndOfFileExpr());
}



/** creates an expression that parses any character.
	@return the appropriate expression.
 */
ExprPtr any()
{
	return ExprPtr(new AnyExpr());
}

ExprPtr debug(std::function<void()> fn)
{
	return ExprPtr(new DebugExpr(fn));
}
#ifdef DEBUG_PARSING
ExprPtr trace_debug(const char *msg, const ExprPtr e)
{
	return ExprPtr(new TraceExpr(msg, e));
}
#endif


/** parses the given input.
	The parse procedures of each rule parsed are executed
	before this function returns, if parsing succeeds.
	@param i input.
	@param g root rule of grammar.
	@param ws whitespace rule.
	@param el list of errors.
	@param d user data, passed to the parse procedures.
	@return true on parsing success, false on failure.
 */
bool parse(Input &i, const Rule &g, const Rule &ws, ErrorReporter &err,
           const ParserDelegate &delegate, void *d)
{
	//prepare context
	Context con(i, ws, delegate);

	//parse initial whitespace
	con.parse_term(con.whitespace_rule);

	//parse grammar
	if (!con.parse_non_term(g))
	{
		_syntax_Error(err, con);
		return false;
	}

	//parse whitespace at the end
	con.parse_term(con.whitespace_rule);

	//if end is not reached, there was an error
	if (!con.end())
	{
		if (con.error_pos.it < con.finish)
		{
			_syntax_Error(err, con);
		}
		else
		{
			_eof_Error(err, con);
		}
		return false;
	}

	con.clear_cache();

	//success; execute the parse procedures
	return con.do_parse_procs(d);
}

ParserDelegate::~ParserDelegate() {}

static inline bool parseCharacter(Context &con, char32_t character)
{
	if (!con.end())
	{
		char32_t ch = con.symbol();
		if (ch == character)
		{
			con.next_col();
			return true;
		}
	}
	con.set_error_pos();
	return false;
}
bool CharacterExpr::parse_non_term(Context &con) const
{
	return parseCharacter(con, character);
}

bool CharacterExpr::parse_term(Context &con) const
{
	return parseCharacter(con, character);
}
void CharacterExpr::dump() const
{
	fprintf(stderr, "'%c'", static_cast<char>(character));
}

ExprPtr CharacterExpr::operator-(const CharacterExpr &other)
{
	return range(character, other.character);
}
ExprPtr CharacterExpr::operator-(char32_t other)
{
	return range(character, other);
}



} //namespace pegmatite

