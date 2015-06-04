/*-
 * Copyright (c) 2012, Achilleas Margaritis
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
#ifndef PEGMATITE_PARSER_HPP
#define PEGMATITE_PARSER_HPP


#include <vector>
#include <string>
#include <list>
#include <functional>
#include <memory>


namespace pegmatite {


class Expr;
class Context;
class Rule;


/**
 * Abstract superclass for indexing into a buffer with arbitrary storage.
 * The class holds a private buffer of characters and requests that the
 * subclass fill it in, or provide direct access to the underlying storage if
 * it is in the correct format.
 */
class Input
{
	protected:
	/**
	 * Size for the static buffer.  Note that changing this will change the
	 * ABI, so do not change it in shared library builds!
	 */
	static const std::size_t static_buffer_size = 512;
	public:
	/**
	 * The type of indexes into the buffer.
	 */
	typedef size_t Index;
	static const Index npos = static_cast<Index>(-1);
	/**
	 * Iterator, refers back into the input stream.
	 */
	class iterator : public std::iterator<std::bidirectional_iterator_tag, char32_t>
	{
		friend Input;
		/**
		 * The buffer that this iterator refers to.
		 */
		Input	 *buffer;
		/**
		 * The index into the buffer.
		 */
		Index	  idx;
		/**
		 * Constructs a new iterator into the specified input.
		 */
		inline iterator(Input *b, Index i) : buffer(b), idx(i) {}
		public:
		/**
		 * Default constructor, constructs an invalid iterator into no buffer.
		 */
		inline iterator() : buffer(0), idx(npos) {}
		/**
		 * Dereference operator, returns the character represented by this
		 * index.
		 */
		inline char32_t  operator*() const { return (*buffer)[idx]; }
		/**
		 * Move the iterator on to the next location.  Note that this does
		 * *not* check for validity, so will allow constructing iterators past
		 * the end of the permitted.  They will fail on dereference (this check
		 * is performed in the `Input` class - iterators perform no validation.
		 */
		inline iterator &operator++()
		{
			idx++;
			return *this;
		}
	        inline iterator operator++(int /* dummy */)
		{
			iterator copy = *this;
			idx++;
			return copy;
		}
		/**
		 * Move the iterator forward by the specified amount.
		 */
		inline iterator &operator+=(size_t amount)
		{
			idx += amount;
			return *this;
		}
		/**
		 * Move the iterator to the previous location.  Note that this does not
		 * check validity.
		 */
		inline iterator &operator--()
		{
			idx--;
			return *this;
		}
		/**
		 * Compares iterators for equality.  Iterators are equal if they are
		 * the same index in the same buffer.
		 */
		inline bool operator==(const iterator &other) const
		{
			return (buffer == other.buffer) && (idx == other.idx);
		}
		/**
		 * Compares iterators for inequality.
		 */
		inline bool operator!=(const iterator &other) const
		{
			return !(*this == other);
		}
		/**
		 * Compares locations of iterators in the input.
		 */
		inline bool operator>(const iterator &other) const
		{
			return (idx > other.idx);
		}
		/**
		 * Compares locations of iterators in the input.
		 */
		inline bool operator<(const iterator &other) const
		{
			return (idx < other.idx);
		}
		/**
		 * Subtracts one iterator from another, 
		 */
		inline Index operator-(const iterator &other) const
		{
			return idx-other.idx;
		}
		/**
		 * Returns the index into the input stream.
		 */
		Index index() const { return idx; }
	};
	/**
	 * Returns an iterator for the start of the input.
	 */
	inline iterator begin()
	{
		return iterator(this, 0);
	}
	/**
	 * Returns an iterator for the end of the input.
	 */
	inline iterator end()
	{
		return iterator(this, size());
	}
	/**
	 * Fetch the character at the specified index.  This is intended to be
	 * inlined and returns the character from the cached buffer if possible,
	 * falling back to the (non-inlined) slow path if not.
	 */
	inline char32_t operator[](Index n)
	{
		// If the local buffer can satisfy the request, fetch the value
		if ((n >= buffer_start) && (n < buffer_end))
		{
			return buffer[n - buffer_start];
		}
		return slowCharacterLookup(n);
	}
	/**
	 * Default constructor, sets the buffer start to be after the buffer end,
	 * so that the first request will trigger a fetch from the underlying
	 * storage.
	 */
	Input() : buffer(0), buffer_start(1), buffer_end(0) {}
	private:
	/**
	 * A pointer to the start of the buffer.  This must be a contiguous block
	 * of memory, storing 32-bit characters.
	 */
	char32_t *buffer;
	/**
	 * The index of the start of the buffer.
	 */
	Index	 buffer_start;
	/**
	 * The index of the end of the buffer.
	 */
	Index	 buffer_end;
	/**
	 * A buffer that can be used to store characters by subclasses that do not
	 * have the same underlying representation.
	 */
	char32_t  local_buffer[static_buffer_size];
	/**
	 * Slow path, filling in the buffer from the data source if a request can't
	 * be satisfied from the cache.
	 */
	char32_t  slowCharacterLookup(Index n);
	protected:
	/**
	 * Fill in the buffer with the next range.  This is called when the current
	 * cached buffer does not contain the range.  The function returns true if
	 * it can provide at least one character from the index specified by start.
	 * The length of the buffer passed in via the last parameter is provided as
	 * the second argument.
	 *
	 * Implementations of this function may either fill in the provided buffer,
	 * reducing the value passed by the second parameter if there are not
	 * enough characters available to satisfy it, or set the third parameter to
	 * refer to their underlying storage.
	 */
	virtual bool  fillBuffer(Index start, Index &length, char32_t *&b) = 0;
	/**
	 * Returns the size of the buffer.
	 */
	virtual Index size() const = 0;
	/**
	 * Virtual destructor.
	 */
	virtual ~Input();
};

/**
 * A concrete input class that wraps a vector of 32-bit characters.
 */
class UnicodeVectorInput : public Input
{
	/**
	 * A reference to the vector being used as input.
	 */
	const std::vector<char32_t> vector;
	public:
	/**
	 * Returns an immutable reference to the vector.  The returned value is
	 * guaranteed to remain valid and not be modified for as long as the input
	 * object exists.
	 */
	const std::vector<char32_t> &getVector() { return vector; }
	/**
	 * Constructs the wrapper from a vector.  
	 * The new object takes ownership of the character data in the vector.
	 */
	UnicodeVectorInput(std::vector<char32_t> &&v) : vector(v) {}
	/**
	 * Provides direct access to the underlying vector's storage.
	 */
	bool  fillBuffer(Index start, Index &length, char32_t *&b) override;
	/**
	 * Returns the size of the vector.
	 */
	Index size() const override;
};

/**
 * A concrete `Input` class that wraps a file.  The file is assumed to be in
 * ASCII.  Note that this does *NOT* include UTF-8 unless it is restricted to
 * the 7-bit subset, as individual characters are promoted directly.  There is
 * no support in this class for characters that require longer encodings.
 */
struct AsciiFileInput : public Input
{
	/**
	 * Construct a parser input from a specified file descriptor.
	 */
	AsciiFileInput(int file);
	bool  fillBuffer(Index start, Index &length, char32_t *&b) override;
	Index size() const override;
	private:
	/**
	 * The file descriptor for the file that this encapsulates.
	 */
	int fd;
	/**
	 * The size of the file.
	 */
	size_t file_size;
};

/**
 * A concrete Input subclass that wraps a std::string, providing access to the
 * underlying characters.
 */
class StringInput : public Input
{
	/**
	 * The string representing the underlying data.
	 */
	const std::string str;
	public:
	/**
	 * Returns an immutable reference to the vector.  The returned value is
	 * guaranteed to remain valid and not be modified for as long as the input
	 * object exists.
	 */
	const std::string &getString() { return str; }
	/**
	 * Constructs the wrapper from a string (`s`).  
	 * The new object takes ownership of the character data in the string.
	 */
	StringInput(std::string &&s) : str(s) {}
	/**
	 * Constructs the wrapper from a string (`s`).
	 * The new object is copy-constructed from the string argument.
	 */
	StringInput(const std::string& s) : str(s) {}
	/**
	 * Provides direct access to the underlying string's storage.
	 */
	bool  fillBuffer(Index start, Index &length, char32_t *&b) override;
	/**
	 * Returns the size of the string.
	 */
	Index size() const override;
};

template<class T>
class IteratorInput : public Input
{
	/**
	 * The iterator for the start of the input.
	 */
	T begin;
	/**
	 * The iterator for the end of the input.
	 */
	T end;
	public:
	/**
	 * Construct an input that reads from between the two iterators specified.
	 */
	IteratorInput(T b, T e) : begin(b), end(e) {}
	/**
	 * Copy the data into the buffer.
	 */
	bool fillBuffer(Index start, Index &length, char32_t *&b) override
	{
		if (start > static_cast<Index>(end-begin))
		{
			length = 0;
			return false;
		}
		Index copied = 0;
		for (T i=(begin+start) ; (i != end) && (copied < length) ; ++i)
		{
			b[copied++] = static_cast<char32_t>(*i);
		}
		length = copied;
		return true;
	}
	/**
	 * Returns the size of the string.
	 */
	Index size() const override { return static_cast<Index>(end - begin); }
};


///position into the input.
struct ParserPosition
{
	///iterator into the input.
	Input::iterator it;

	///line.
	int line;

	///column.
	int col;

	///null constructor.
	ParserPosition() {}

	/** constructor from input.
		@param i input.
	 */
	ParserPosition(Input &i);
};

/**
 * The callback that handles matches.  The arguments are the start and end of
 * the matching range and some state for the current parse.
 * When constructing an AST using the AST infrastructure, this will be a lambda
 * binding the type of the AST node to create and the argument will be
 * interpreted as an AST stack.
 */
typedef std::function<void(const ParserPosition&,
                           const ParserPosition&, void*)> parse_proc;


/**
 * A range within input.  This is passed to `construct()` methods for AST
 * classes and allows terminals to record the source location.
 */
class InputRange
{
public:
	///begin position.
	ParserPosition start;

	///end position.
	ParserPosition finish;

	///empty constructor.
	InputRange() {}

	/** constructor.bbb
		@param b begin position.
		@param e end position.
	 */
	InputRange(const ParserPosition &b, const ParserPosition &e);
	/**
	 * Iterator to the start of the input range.
	 */
	Input::iterator begin() const { return start.it; };
	/**
	 * Iterator to the end of the input range.
	 */
	Input::iterator end() const { return finish.it; };
};


///enum with error types.
enum ERROR_TYPE
{
	///syntax error
	ERROR_SYNTAX_ERROR = 1,

	///invalid end of file
	ERROR_INVALID_EOF,

	///first user error
	ERROR_USER = 100
};


///Error.
class Error : public InputRange
{
public:
	///type
	int error_type;

	/** constructor.
		@param b begin position.
		@param e end position.
		@param t type.
	 */
	Error(const ParserPosition &b, const ParserPosition &e, int t);

	/** compare on begin position.
		@param e the other error to compare this with.
		@return true if this comes before the previous error, false otherwise.
	 */
	bool operator < (const Error &e) const;
};


///type of error list.
typedef std::list<Error> ErrorList;

/**
 * CharacterExpr is a concrete subclass of Expr, which is exposed to allow it to be 
 */
class CharacterExpr;
typedef std::shared_ptr<CharacterExpr> CharacterExprPtr;

/**
 * A shared pointer to an expression.  All expression tree nodes use shared
 * pointers.  
 */
struct ExprPtr : public std::shared_ptr<Expr>
{
	/**
	 * Construct an expression pointer wrapping an expression.
	 */
	ExprPtr(Expr *e) : std::shared_ptr<Expr>(e) {}
	/**
	 * Construct an expression pointer wrapping an expression that refers to a rule.
	 */
	ExprPtr(const Rule &e);
	/**
	 * Construct an expression pointer wrapping a character expression.
	 */
	ExprPtr(const CharacterExprPtr &e);
	/**
	 * Construct an expression pointer wrapping a string expression created
	 * from a string.
	 */
	ExprPtr(const char *);
	/**
	 * Construct an expression pointer wrapping a character expression created
	 * from a character.
	 */
	ExprPtr(const char);
};


/**
 * Rule class, which represents a rule in a grammar.  Rules are distinct from
 * expressions, in that they are expected to be top-level constructs that can
 * have actions associated with them.
 */
class Rule
{
public:
	/** 
	 * Constructs a rule, owning a reference to an expression.  Expressions may
	 * be shared between rules.
	 */
	Rule(const ExprPtr e);
	/**
	 * Constructor for creating rules from character expressions.
	 */
	Rule(const CharacterExprPtr e) : Rule(ExprPtr(e)) {}
	/**
	 * Copying rules is not allowed.  Attempting to do so creates a new rule referencing the old.
	 */
	Rule(const Rule &r) : Rule(ExprPtr(r)) {}
	/**
	 * Move constructor for a rule, allows `rulename = {some expression}`
	 * initialisation without performing copying.
	 */
	Rule(const Rule &&r);
private:
	/**
	 * The expression that this rule invokes.
	 */
	const ExprPtr expr;

	/**
	 * Copying rules is not allowed.
	 */
	Rule &operator = (const Rule &) = delete;

	friend class Context;
};

/**
 * Abstract base class for expressions.
 */
class Expr
{
public:
	/**
	 * Virtual destructor for safe overloading.  Note that generally expression
	 * objects are not meant to be deallocated, as they can be used by multiple
	 * parsers. 
	 */
	virtual ~Expr() { }

	/**
	 * Parse this expression as a non-terminal.  Non-terminals are permitted to
	 * contain whitespace around compound expressions, as defined by the
	 * whitespace rule in the context.
	 */
	virtual bool parse_non_term(Context &con) const = 0;

	/**
	 * Parse this expression as a terminal.  Terminals are exact matches for
	 * the specified expression, without any whitespace.
	 */
	virtual bool parse_term(Context &con) const = 0;

	/**
	 * Dump the current rule.  Used for debugging.
	 */
	virtual void dump() const = 0;

};
/** creates a zero-or-more loop out of this expression.
	@return a zero-or-more loop expression.
 */
ExprPtr operator *(const ExprPtr &e);
inline ExprPtr operator *(const Rule &r)
{
	return *ExprPtr(r);
}

/** creates a one-or-more loop out of this expression.
	@return a one-or-more loop expression.
 */
ExprPtr operator +(const ExprPtr &e);
inline ExprPtr operator +(const Rule &r)
{
	return +ExprPtr(r);
}

/** creates an optional out of this expression.
	@return an optional expression.
 */
ExprPtr operator -(const ExprPtr &e);
inline ExprPtr operator -(const Rule &r)
{
	return -ExprPtr(r);
}

/** creates an AND-expression.
	@return an AND-expression.
 */
ExprPtr operator &(const ExprPtr &e);
inline ExprPtr operator &(const Rule &r)
{
	return &ExprPtr(r);
}

/** creates a NOT-expression.
	@return a NOT-expression.
 */
ExprPtr operator !(const ExprPtr &e);
inline ExprPtr operator !(const Rule &r)
{
	return !ExprPtr(r);
}


CharacterExprPtr operator "" _E(const char x);
/**
 * Constructs a set expression.
 */
ExprPtr operator "" _S(const char *x, std::size_t len);
/**
 * Constructs a literal expression.
 */
ExprPtr operator "" _E(const char *x, std::size_t len);
/**
 * Constructs a regular expression.
 */
ExprPtr operator "" _R(const char *x, std::size_t len);
ExprPtr operator-(const CharacterExprPtr &left, const CharacterExprPtr &right);
ExprPtr operator-(const CharacterExprPtr &left, int right);


/**
 * Traces attempts to parse an expression, logging a message when the rule is
 * entered and exited.
 */
#ifdef DEBUG_PARSING
ExprPtr trace_debug(const char *msg, const ExprPtr e);
inline ExprPtr trace(const char *msg, const ExprPtr e)
{
	return trace_debug(msg, e);
}
#else
inline ExprPtr trace(const char *, const ExprPtr e)
{
	return e;
}
#endif


/** creates a sequence of expressions.
	@param left left operand.
	@param right right operand.
	@return an expression which parses a sequence.
 */
ExprPtr operator >> (const ExprPtr &left, const ExprPtr &right);

/** creates a choice of expressions.
	@param left left operand.
	@param right right operand.
	@return an expression which parses a choice.
 */
ExprPtr operator | (const ExprPtr &left, const ExprPtr &right);

/** converts a parser expression into a terminal.
	@param e expression.
	@return an expression which parses a terminal.
 */
ExprPtr term(const ExprPtr &e);


/** creates a set expression from a null-terminated string.
	@param s null-terminated string with characters of the set.
	@return an expression which parses a single character out of a set.
 */
ExprPtr set(const char *s);

/**
 * Creates a regex expression from the specified string.
 */
ExprPtr regex(const char *s);

/**
 * Creates a regex expression from the specified wide string.
 */
ExprPtr regex(const wchar_t *s);


/** creates a set expression from a null-terminated wide string.
	@param s null-terminated string with characters of the set.
	@return an expression which parses a single character out of a set.
 */
ExprPtr set(const wchar_t *s);


/** creates a range expression.
	@param min min character.
	@param max max character.
	@return an expression which parses a single character out of range.
 */
ExprPtr range(int min, int max);


/** creates an expression which increments the line counter
	and resets the column counter when the given expression
	is parsed successfully; used for newline characters.
	@param e expression to wrap into a newline parser.
	@return an expression that handles newlines.
 */
ExprPtr nl(const ExprPtr &e);


/** creates an expression which tests for the end of input.
	@return an expression that handles the end of input.
 */
ExprPtr eof();


/** creates an expression that parses any character.
	@return the appropriate expression.
 */
ExprPtr any();

/**
 * Returns a new expression that is always successfully matched and executes
 * the argument function when it is matched.
 */
ExprPtr debug(std::function<void()> fn);

/**
 * Parser delegate abstract class.  Subclasses of this are responsible for
 * providing handlers for the rules in the grammar.
 */
struct ParserDelegate
{
	/**
	 * Returns the handler for the specified rule.
	 */
	virtual parse_proc get_parse_proc(const Rule &) const = 0;
	/**
	 * Virtual destructor, for cleaning up subclasses correctly.
	 */
	virtual ~ParserDelegate();
};

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
bool parse(Input &i, const Rule &g, const Rule &ws, ErrorList &el,
           const ParserDelegate &delegate, void *d);


/** output the specific input range to the specific stream.
	@param stream stream.
	@param ir input range.
	@return the stream.
 */
template <class T> T &operator << (T &stream, const InputRange &ir)
{
	for(auto c : ir)
	{
		stream << static_cast<typename T::char_type>(c);
	}
	return stream;
}



} //namespace pegmatite


#endif //PEGMATITE_PARSER_HPP
