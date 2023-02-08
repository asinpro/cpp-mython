#include "statement.h"
#include "runtime.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std;

namespace ast {

using runtime::Closure;
using runtime::Context;
using runtime::ObjectHolder;

namespace {
const string ADD_METHOD = "__add__"s;
const string INIT_METHOD = "__init__"s;
}  // namespace

ObjectHolder Assignment::Execute(Closure& closure, Context& context) {
    return closure[var_] = rv_->Execute(closure, context);
}

Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv) :
    var_(move(var)),
    rv_(move(rv)) {
}

VariableValue::VariableValue(const std::string& var_name) :
    dotted_ids_({var_name}) {
}

VariableValue::VariableValue(std::vector<std::string> dotted_ids) :
    dotted_ids_(move(dotted_ids)) {
}

ObjectHolder VariableValue::Execute(Closure& closure, Context& /*context*/) {
    auto object = GetVarByName(closure, dotted_ids_.front());
    return GetVarByNameRange(dotted_ids_.begin() + 1, dotted_ids_.end(), object);
}

runtime::ObjectHolder VariableValue::GetVarByName(runtime::Closure& closure, const string& name) {
    if (closure.count(name) == 0) {
        throw runtime_error("Name " + name + " is not defined"s);
    }

    return closure[name];
}

unique_ptr<Print> Print::Variable(const std::string& name) {
    return make_unique<Print>(make_unique<VariableValue>(name));
}

Print::Print(unique_ptr<Statement> argument) {
    args_.push_back(move(argument));
}

Print::Print(vector<unique_ptr<Statement>> args) :
    args_(move(args)) {
}

ObjectHolder Print::Execute(Closure& closure, Context& context) {
    auto& os = context.GetOutputStream();
    bool is_first = true;

    for (auto& arg : args_) {
        if (!is_first) {
            os << ' ';
        }
        is_first = false;

        if (auto object = arg->Execute(closure, context)) {
            object->Print(os, context);
        } else {
            os << "None"s;
        }
    }

    os << endl;

    return ObjectHolder::None();
}

MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                       std::vector<std::unique_ptr<Statement>> args) :
    object_(move(object)),
    method_(move(method)),
    args_(move(args)) {
}

ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
    vector<ObjectHolder> arguments;
    arguments.reserve(args_.size());

    for (auto& arg : args_) {
        arguments.push_back(arg->Execute(closure, context));
    }

    return object_->Execute(closure, context).TryAs<runtime::ClassInstance>()->Call(method_, arguments, context);
}

ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
    if (auto object = argument_->Execute(closure, context)) {
        ostringstream os;
        object->Print(os, context);
        return ObjectHolder::Own(runtime::String(os.str()));
    }

    return ObjectHolder::Own(runtime::String("None"s));
}

ObjectHolder Add::Execute(Closure& closure, Context& context) {
    auto lhs = lhs_->Execute(closure, context);
    auto rhs = rhs_->Execute(closure, context);

    if (lhs && rhs) {
        if (auto result = Addition<runtime::Number>(lhs, rhs)) {
            return *result;
        } if (auto result = Addition<runtime::String>(lhs, rhs)) {
            return *result;
        } else if (auto* left = lhs.TryAs<runtime::ClassInstance>()) {
            if (left->HasMethod(ADD_METHOD, 1U)) {
                return left->Call(ADD_METHOD, {rhs}, context);
            }
        }
    }

    throw runtime_error("Unsupported operand type(s) for +: 'int' and 'str'"s);
}

ObjectHolder Sub::Execute(Closure& closure, Context& context) {
    auto lhs = lhs_->Execute(closure, context);
    auto rhs = rhs_->Execute(closure, context);

    if (lhs && rhs) {
        if (auto* left = lhs.TryAs<runtime::Number>()) {
            if (auto* right = rhs.TryAs<runtime::Number>()) {
                return runtime::ObjectHolder::Own(runtime::Number(left->GetValue() - right->GetValue()));
            }
        }
    }

    throw std::runtime_error("Unsupported operand type(s) for -");
}

ObjectHolder Mult::Execute(Closure& closure, Context& context) {
    auto lhs = lhs_->Execute(closure, context);
    auto rhs = rhs_->Execute(closure, context);

    if (lhs && rhs) {
        if (auto* left = lhs.TryAs<runtime::Number>()) {
            if (auto* right = rhs.TryAs<runtime::Number>()) {
                return runtime::ObjectHolder::Own(runtime::Number(left->GetValue() * right->GetValue()));
            }
        }
    }

    throw std::runtime_error("Unsupported operand type(s) for *");
}

ObjectHolder Div::Execute(Closure& closure, Context& context) {
    auto lhs = lhs_->Execute(closure, context);
    auto rhs = rhs_->Execute(closure, context);

    if (lhs && rhs) {
        if (auto* left = lhs.TryAs<runtime::Number>()) {
            if (auto* right = rhs.TryAs<runtime::Number>()) {
                if (right->GetValue() > 0) {
                    return runtime::ObjectHolder::Own(runtime::Number(left->GetValue() / right->GetValue()));
                }

                throw std::runtime_error("Division by zero");
            }
        }
    }

    throw std::runtime_error("Unsupported operand type(s) for /");
}

ObjectHolder Compound::Execute(Closure& closure, Context& context) {
    for (auto& statement : statements_) {
        statement->Execute(closure, context);
    }

    return ObjectHolder::None();
}

ObjectHolder Return::Execute(Closure& closure, Context& context) {
    throw ReturnException{statement_->Execute(closure, context)};
}

ClassDefinition::ClassDefinition(ObjectHolder cls) :
    cls_(move(cls)) {
}

ObjectHolder ClassDefinition::Execute(Closure& closure, Context& /*context*/) {
    return closure[cls_.TryAs<runtime::Class>()->GetName()] = cls_;
}

FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
                                 std::unique_ptr<Statement> rv) :
    object_(move(object)),
    field_name_(move(field_name)),
    rv_(move(rv)) {
}

ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
    auto object = object_.Execute(closure, context);
    auto* instance = object.TryAs<runtime::ClassInstance>();
    return instance->Fields()[field_name_] = rv_->Execute(closure, context);
}

IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
               std::unique_ptr<Statement> else_body) :
    condition_(move(condition)),
    if_body_(move(if_body)),
    else_body_(move(else_body)) {
}

ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
    if (IsTrue(condition_->Execute(closure, context))) {
        return if_body_->Execute(closure, context);
    }

    return else_body_ ? else_body_->Execute(closure, context) : ObjectHolder::None();
}

ObjectHolder Or::Execute(Closure& closure, Context& context) {
    return ObjectHolder::Own(runtime::Bool(
        IsTrue(lhs_->Execute(closure, context)) ||
        IsTrue(rhs_->Execute(closure, context))
    ));
}

ObjectHolder And::Execute(Closure& closure, Context& context) {
    return ObjectHolder::Own(runtime::Bool(
        IsTrue(lhs_->Execute(closure, context)) &&
        IsTrue(rhs_->Execute(closure, context))
    ));
}

ObjectHolder Not::Execute(Closure& closure, Context& context) {
    return ObjectHolder::Own(runtime::Bool(
        !IsTrue(argument_->Execute(closure, context))
    ));
}

Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
    : BinaryOperation(std::move(lhs), std::move(rhs)),
    cmp_(cmp) {
}

ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
    return ObjectHolder::Own(runtime::Bool(cmp_(
        lhs_->Execute(closure, context),
        rhs_->Execute(closure, context),
        context
    )));
}

NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<Statement>> args) :
    class_(class_),
    args_(move(args)) {
}

NewInstance::NewInstance(const runtime::Class& class_) :
    class_(class_) {
}

ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
    auto object = ObjectHolder::Own(runtime::ClassInstance{class_});
    auto* instance = object.TryAs<runtime::ClassInstance>();

    if (instance->HasMethod(INIT_METHOD, args_.size())) {
        vector<ObjectHolder> params;

        for (auto& var : args_) {
            params.push_back(var->Execute(closure, context));
        }

        instance->Call(INIT_METHOD, params, context);
    }

    return object;
}

MethodBody::MethodBody(std::unique_ptr<Statement>&& body) :
    body_(move(body)) {
}

ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
    try {
        body_->Execute(closure, context);
        return ObjectHolder::None();
    } catch(const Return::ReturnException& e) {
        return e.result;
    }
}

}  // namespace ast
