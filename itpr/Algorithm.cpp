#include "Algorithm.h"

#include "Exceptions.h"

namespace moon {
namespace itpr {

    Value CallFunction(
        Scope& scope,
        Stack& stack,
        const SourceLocation& location,
        const std::string& symbol,
        const std::vector<Value>& argValues)
    {
        // Analyze function value.
        Value functionValue = scope.GetValue(symbol, location, stack);
        if (!IsFunction(functionValue)) {
            throw ExScopeSymbolIsNotFunction{ location, stack };
        }

        if (argValues.size() > functionValue.GetFuncArity()) {
            throw ExScopeFormalActualArgCountMismatch{ location, stack };
        }

        // Analyze function definition.
        const auto& functionDef = functionValue.GetFuncDef();
        const auto& captures = functionValue.GetFuncCaptures();
        auto applArgs = functionValue.GetAppliedArgs();
        std::copy(begin(argValues), end(argValues), std::back_inserter(applArgs));

        if (argValues.size() < functionValue.GetFuncArity()) {
            // "Curry on"...            
            return Value::MakeFunction(&functionDef, captures, applArgs);
        }

        // Call function
        const auto& argNames = functionDef.GetFormalArgs();
        const auto& argLocations = functionDef.GetArgLocations();
        assert(argNames.size() == argLocations.size());

        unsigned argsCount = argNames.size();

        LocalScope funcScope{ scope.GetGlobalScope() };

        for (unsigned i = 0; i < argsCount; ++i) {
            funcScope.TryRegisteringBind(
                stack, 
                argNames[i], 
                applArgs[i], 
                argLocations[i]);
        }

        for (const auto& pr : captures) {
            funcScope.TryRegisteringBind(
                stack, 
                pr.first, 
                pr.second.value, 
                pr.second.location);
        }

        stack.PushCall(symbol);
        Value result = functionDef.Execute(funcScope, stack);
        stack.PopCall();

        return result;
    }

}
}
