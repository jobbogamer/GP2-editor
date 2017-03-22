
#include "tracestep.hpp"

namespace Developer {

TraceStepType stepTypeFromXML(QStringRef elementName) {
    if      (elementName == "rule")      { return RULE; }
    else if (elementName == "match")     { return RULE_MATCH; }
    else if (elementName == "apply")     { return RULE_APPLICATION; }
    else if (elementName == "ruleset")   { return RULE_SET; }
    else if (elementName == "loop")      { return LOOP; }
    else if (elementName == "iteration") { return LOOP_ITERATION; }
    else if (elementName == "procedure") { return PROCEDURE; }
    else if (elementName == "if")        { return IF_CONTEXT; }
    else if (elementName == "try")       { return TRY_CONTEXT; }
    else if (elementName == "condition") { return BRANCH_CONDITION; }
    else if (elementName == "then")      { return THEN_BRANCH; }
    else if (elementName == "else")      { return ELSE_BRANCH; }
    else if (elementName == "or")        { return OR_CONTEXT; }
    else if (elementName == "left")      { return OR_LEFT; }
    else if (elementName == "right")     { return OR_RIGHT; }
    else if (elementName == "skip")      { return SKIP; }
    else if (elementName == "break")     { return BREAK; }
    else if (elementName == "fail")      { return FAIL; }

    else { return UNKNOWN; }
}

}
