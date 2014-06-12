#ifndef MOON_ITPR_STACK_FRAME_H
#define MOON_ITPR_STACK_FRAME_H

#include <string>

namespace moon {
namespace itpr {

    class StackFrame {
        const std::string m_function;
    public:
        StackFrame(std::string function) :
            m_function{ function }
        {}

        const std::string GetFunction() const
        {
            return m_function;
        }
    };

}
}

#endif
