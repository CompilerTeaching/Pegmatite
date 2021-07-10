/**
 * @file calculator.cc
 * Simple integration test based on the `calculator` example.
 *
 * RUN: %cxx %s -o %t.exe
 * RUN: %t.exe > %t.out
 * RUN: %FileCheck -input-file %t.out %s
 */

#include <iostream>
#include <sstream>
#include <string>
#include "pegmatite.hh"

using namespace std;
using namespace pegmatite;


namespace AST
{
template<typename T>
class Expression : public ASTContainer
{
public:
	virtual T eval() const = 0;
	virtual void print(size_t depth = 0) const = 0;
	PEGMATITE_RTTI(Expression, ASTContainer)
};


template<typename T>
class Number : public Expression<T>
{
	ASTValue<T> n;
public:
	T eval() const override
	{
		return n.value;
	}

	void print(size_t depth) const override
	{
		cout << string(depth, '\t') << n.value << endl;
	}
};


template<typename T, class Func, char op>
class BinaryExpression : public Expression<T>
{
	ASTPtr<Expression<T>> left, right;
public:
	T eval() const override
	{
		Func f;
		return f(left->eval(), right->eval());
	}

	void print(size_t depth=0) const override
	{
		cout << string(depth, '\t') << op << endl;
		left->print(depth+1);
		right->print(depth+1);
	}
};

}

namespace Parser
{
struct CalculatorGrammar
{
	Rule ws     = " \t\n"_E;
	Rule digits  = "[0-9]+"_R;
	Rule num    = digits >> -('.'_E >> digits >> -("eE"_S >> -("+-"_S) >> digits));
	Rule val    = num |  '(' >> expr >> ')';
	Rule mul_op = mul >> '*' >> mul;
	Rule div_op = mul >> '/' >> mul;
	Rule mul    = mul_op | div_op | val;
	Rule add_op = expr >> '+' >> expr;
	Rule sub_op = expr >> '-' >> expr;
	Rule expr   = add_op | sub_op | mul;

	static const CalculatorGrammar& get()
	{
		static CalculatorGrammar g;
		return g;
	}

	protected:
	CalculatorGrammar() {}
};

template<class T>
struct CalculatorParser : public ASTParserDelegate
{
	const CalculatorGrammar &g = CalculatorGrammar::get();

	BindAST<AST::Number<T>> num = g.num;
	BindAST<AST::BinaryExpression<T,plus<T>,'+'>> add = g.add_op;
	BindAST<AST::BinaryExpression<T,minus<T>,'-'>> sub = g.sub_op;
	BindAST<AST::BinaryExpression<T,multiplies<T>,'*'>> mul = g.mul_op;
	BindAST<AST::BinaryExpression<T,divides<T>,'/'>> div = g.div_op;
};

struct IntCalculatorGrammar : public CalculatorGrammar
{
	Rule mod_op = mul >> '%' >> mul;

	static const IntCalculatorGrammar& get()
	{
		static IntCalculatorGrammar g;
		return g;
	}

	private:
	IntCalculatorGrammar() : CalculatorGrammar()
	{
		// The `mul` rule should now use the mod operation.
		mul    = mul_op | div_op | mod_op | val;
		// In the integer version, we don't support 
		num    = digits >> -("eE"_S >> -("+-"_S) >> digits);
	}
};

struct IntCalculatorParser : public ASTParserDelegate
{
	typedef long long T;
	const IntCalculatorGrammar &g = IntCalculatorGrammar::get();

	BindAST<AST::Number<T>> num = g.num;
	BindAST<AST::BinaryExpression<T,plus<T>,'+'>> add = g.add_op;
	BindAST<AST::BinaryExpression<T,minus<T>,'-'>> sub = g.sub_op;
	BindAST<AST::BinaryExpression<T,multiplies<T>,'*'>> mul = g.mul_op;
	BindAST<AST::BinaryExpression<T,divides<T>,'/'>> div = g.div_op;
	BindAST<AST::BinaryExpression<T,modulus<T>,'%'>> mod = g.mod_op;
};

}

template<class Parser, class Value>
void PrettyPrint(std::string s)
{
	Parser p;
	StringInput i(move(s));

	unique_ptr<AST::Expression<Value>> root;
	if (p.parse(i, p.g.expr, p.g.ws, defaultErrorReporter, root) and root)
	{
		double v = root->eval();
		cout << "result = " << v << endl;
		cout << "parse tree:\n";
		root->print();
		cout << endl;
	}
}

int main()
{
	// CHECK: result = 42
	// CHECK-NEXT: parse tree:
	// CHECK-NEXT:  +
	// CHECK-NEXT:  16
	// CHECK-NEXT:  26
	PrettyPrint<Parser::IntCalculatorParser, long long>("16+26");

	// CHECK: result = 3.4
	// CHECK-NEXT: parse tree:
	// CHECK-NEXT:  +
	// CHECK-NEXT:  1.1
	// CHECK-NEXT:  2.3
	PrettyPrint<Parser::CalculatorParser<double>, double>("1.1+2.3");

	return 0;
}
