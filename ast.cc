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
#include <cassert>
#include <cstdlib>
#include "ast.hh"


namespace {
/**
 * The current AST container.  When constructing an object, this is set and
 * then the constructors for the fields run, accessing it to detect their
 * parents.
 */
thread_local pegmatite::ASTContainer *current = nullptr;
/**
 * The current parser delegate.  When constructing an object, this is set and
 * then the constructors for the fields run, accessing it to detect their
 * parents.
 */
thread_local pegmatite::ASTParserDelegate *currentParserDelegate;
}

namespace pegmatite {


std::string demangle(std::string mangled)
{
	int err;
	size_t s = 0;
	char *buffer = static_cast<char*>(malloc(mangled.length() + 1));

	char *demangled = 
#ifdef __unix__
		abi::__cxa_demangle(mangled.c_str(), buffer, &s, &err);
#else
		nullptr;
	(void)err;
	(void)s;
	(void)buffer;
#endif
	std::string result = demangled ? demangled : mangled;

	free(demangled ? demangled : buffer);

	return result;
}


/**
 * Out-of-line virtual destructor forces vtable to be emitted in this
 * translation unit only.
 */
ASTNode::~ASTNode()
{
}


/** sets the container under construction to be this.
 */
ASTContainer::ASTContainer()
{
	current = this;
}

ASTContainer::~ASTContainer()
{
}


/** Asks all members to construct themselves from the stack.
	The members are asked to construct themselves in reverse order.
	from a node stack.
	@param st stack.
 */
bool ASTContainer::construct(const InputRange &r, ASTStack &st,
                             const ErrorReporter &err)
{
	bool success = true;

	for(auto it = members.rbegin(); it != members.rend(); ++it)
	{
		ASTMember *member = *it;
		success |= member->construct(r, st, err);
	}
	// We don't need the members vector anymore, so clean up the storage it
	// uses.
	members.clear();
	return success;
}

ASTMember::ASTMember()
{
	assert(current && "ASTMember must be contained within an ASTContainer");
	current->members.push_back(this);
}
ASTMember::~ASTMember() {}

ASTParserDelegate::ASTParserDelegate()
{
	currentParserDelegate = this;
}

void ASTParserDelegate::set_parse_proc(const Rule &r, parse_proc p)
{
	handlers[std::addressof(r)] = p;
}
void ASTParserDelegate::bind_parse_proc(const Rule &r, parse_proc p)
{
	currentParserDelegate->set_parse_proc(r, p);
}
parse_proc ASTParserDelegate::get_parse_proc(const Rule &r) const
{
	auto it = handlers.find(std::addressof(r));
	if (it == handlers.end()) return nullptr;
	return it->second;
}

/** parses the given input.
	@param input input.
	@param g root rule of grammar.
	@param ws whitespace rule.
	@param err callback for reporting errors.
	@param d user data, passed to the parse procedures.
	@return pointer to AST node created, or null if there was an Error.
		The return object must be deleted by the caller.
 */
std::unique_ptr<ASTNode> parse(Input &input, const Rule &g, const Rule &ws,
                               ErrorReporter &err, const ParserDelegate &d)
{
	ASTStack st;
	if (!parse(input, g, ws, err, d, &st)) return nullptr;
	if (st.size() > 1)
	{
		int i = 0;
		for (auto &I : st)
		{
			auto *val = I.second.get();
			fprintf(stderr, "[%d] %s\n", i++, typeid(*val).name());
		}
	}
	assert(st.size() == 1);
	return std::move(st[0].second);
}
bool ASTString::construct(const pegmatite::InputRange &r, pegmatite::ASTStack &,
                          const ErrorReporter &)
{
	std::stringstream stream;
	for_each(r.begin(),
	         r.end(),
	         [&](char32_t c) {stream << static_cast<char>(c);});
	this->std::string::operator=(stream.str());

	return true;
}

} //namespace pegmatite
