#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>
#include <algorithm>

using namespace std;

namespace runtime {

ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
    : data_(std::move(data)) {
}

void ObjectHolder::AssertIsValid() const {
    assert(data_ != nullptr);
}

ObjectHolder ObjectHolder::Share(Object& object) {
    // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
    return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
}

ObjectHolder ObjectHolder::None() {
    return ObjectHolder();
}

Object& ObjectHolder::operator*() const {
    AssertIsValid();
    return *Get();
}

Object* ObjectHolder::operator->() const {
    AssertIsValid();
    return Get();
}

Object* ObjectHolder::Get() const {
    return data_.get();
}

ObjectHolder::operator bool() const {
    return Get() != nullptr;
}

bool IsTrue(const ObjectHolder& object) {
    if (const auto* number = object.TryAs<Number>()) {
        return number->GetValue();
    } else if (const auto* boolean = object.TryAs<Bool>()) {
        return boolean->GetValue();
    } else if (const auto* str = object.TryAs<String>()) {
        return !str->GetValue().empty();
    }

    return false;
}

void ClassInstance::Print(std::ostream& os, Context& context) {
    if (HasMethod("__str__"s, 0)) {
        Call("__str__"s, {}, context)->Print(os, context);
    } else {
        os << this;
    }
}

bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
    if (const auto* m = cls_.GetMethod(method)) {
        return m->formal_params.size() == argument_count;
    }

    return false;
}

Closure& ClassInstance::Fields() {
    return fields_;
}

const Closure& ClassInstance::Fields() const {
    return fields_;
}

ClassInstance::ClassInstance(const Class& cls) :
    cls_(cls) {
}

ObjectHolder ClassInstance::Call(const std::string& method,
                                 const std::vector<ObjectHolder>& actual_args,
                                 Context& context) {
    if (HasMethod(method, actual_args.size())) {
        if (const auto* class_method = cls_.GetMethod(method)) {
            Closure closure;
            closure["self"s] = ObjectHolder::Share(*this);

            for (size_t i = 0; i < actual_args.size(); ++i) {
                const auto& param_name = class_method->formal_params[i];
                closure[param_name] = actual_args[i];
            }

            return class_method->body->Execute(closure, context);
        }
    }

    throw std::runtime_error("Method "s + method + " has not found"s);
}

Class::Class(std::string name, std::vector<Method> methods, const Class* parent) :
    name_(move(name)),
    methods_(move(methods)),
    parent_(parent) {
}

const Method* Class::GetMethod(const std::string& name) const {
    auto it = find_if(methods_.begin(), methods_.end(), [&name](const auto& method){
        return method.name == name;
    });

    if (it != methods_.end()) {
        return &*it;
    }

    return parent_ ? parent_->GetMethod(name) : nullptr;
}

[[nodiscard]] const std::string& Class::GetName() const {
    return name_;
}

void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
    os << "Class "sv << GetName();
}

void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
    os << (GetValue() ? "True"sv : "False"sv);
}

bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    if (!lhs && !rhs) {
        return true;
    }

    if (auto* left = lhs.TryAs<Number>()) {
        if (auto* right = rhs.TryAs<Number>()) {
            return left->GetValue() == right->GetValue();
        }
    } else if (auto* left = lhs.TryAs<Bool>()) {
        if (auto* right = rhs.TryAs<Bool>()) {
            return left->GetValue() == right->GetValue();
        }
    } else if (auto* left = lhs.TryAs<String>()) {
        if (auto* right = rhs.TryAs<String>()) {
            return left->GetValue() == right->GetValue();
        }
    } else if (auto* instance = lhs.TryAs<ClassInstance>()) {
        if (instance->HasMethod("__eq__"s, 1U)) {
            auto res = instance->Call("__eq__"s, {rhs}, context);
            return res.TryAs<Bool>()->GetValue();
        }
    }

    throw std::runtime_error("Cannot compare objects for equality"s);
}

bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    if (!lhs || !rhs) {
        throw std::runtime_error("Cannot compare objects for equality 1"s);
    }

    if (auto* left = lhs.TryAs<Number>()) {
        if (auto* right = rhs.TryAs<Number>()) {
            return left->GetValue() < right->GetValue();
        }
    } else if (auto* left = lhs.TryAs<Bool>()) {
        if (auto* right = rhs.TryAs<Bool>()) {
            return static_cast<int>(left->GetValue()) < static_cast<int>(right->GetValue());
        }
    } else if (auto* left = lhs.TryAs<String>()) {
        if (auto* right = rhs.TryAs<String>()) {
            return left->GetValue() < right->GetValue();
        }
    } else if (auto* instance = lhs.TryAs<ClassInstance>()) {
        if (instance->HasMethod("__lt__"s, 1U)) {
            auto res = instance->Call("__lt__"s, {rhs}, context);
            return res.TryAs<Bool>()->GetValue();
        }
    }

    throw std::runtime_error("Cannot compare objects for equality 2"s);
}

bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Equal(lhs, rhs, context);
}

bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context) && NotEqual(lhs, rhs, context);
}

bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return Less(lhs, rhs, context) || Equal(lhs, rhs, context);
}

bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context);
}

}  // namespace runtime
