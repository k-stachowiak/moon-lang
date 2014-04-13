#include "AstParser.h"

#include "../../itpr/AstFuncDef.h"
#include "../../itpr/Stack.h"

#include "../StrIter.h"
#include "Token.h"
#include "DomNode.h"
#include "Algorithm.h"

namespace moon {
namespace parse {
namespace sexpr {

	std::unique_ptr<itpr::CScope> CAstParser::Parse(
		const std::string& source,
		itpr::CScope* parentScope) const
	{
		std::vector<CToken> tokens = Tokenize(CStrIter::Begin(source), CStrIter::End(source));
		std::vector<CDomNode> domNodes = BuildDom(tokens);
		std::unique_ptr<itpr::CScope> result{ new itpr::CScope{ parentScope } };
		for (const CDomNode& domNode : domNodes) {
			std::unique_ptr<itpr::CAstBind> bind = ParseBind(domNode);
			result->RegisterBind(
					bind->GetLocation(),
					bind->GetSymbol(),
					std::move(bind->TakeOverExpression()));
		}

		return result;
	}

}
}
}