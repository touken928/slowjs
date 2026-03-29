#pragma once
#include <js_types.h>

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace qjs {

struct FuncBase {
    virtual ~FuncBase() = default;
    virtual JSValue call(JSContext*, int, JSValue*) = 0;
    virtual int arity() const = 0;
};

template <typename Tuple, size_t... I>
bool unpack_impl(JSContext* c, JSValue* v, Tuple& result, std::index_sequence<I...>) {
    bool ok = true;
    ((ok = ok && [&]() {
        bool elemOk = false;
        std::get<I>(result) = JSConv<decay_t<std::tuple_element_t<I, Tuple>>>::from(c, v[I], elemOk);
        return elemOk;
    }()), ...);
    return ok;
}

template <typename... Args>
std::tuple<decay_t<Args>...> unpackArgs(JSContext* c, JSValue* v, bool& ok) {
    std::tuple<decay_t<Args>...> result{};
    ok = unpack_impl(c, v, result, std::index_sequence_for<Args...>{});
    return result;
}

template <typename Ret, typename... Args>
struct FuncWrap : FuncBase {
    std::function<Ret(Args...)> fn;
    explicit FuncWrap(std::function<Ret(Args...)> f) : fn(std::move(f)) {}
    int arity() const override { return (int)sizeof...(Args); }
    JSValue call(JSContext* c, int argc, JSValue* argv) override {
        constexpr int expected = (int)sizeof...(Args);
        if (argc != expected) return JS_ThrowTypeError(c, "expected exactly %d arguments, got %d", expected, argc);
        bool ok = false;
        auto args = unpackArgs<Args...>(c, argv, ok);
        if (!ok) {
            if (!JS_IsException(JS_GetException(c))) return JS_ThrowTypeError(c, "argument type conversion failed");
            return JS_EXCEPTION;
        }
        if constexpr (std::is_void_v<Ret>) {
            std::apply(fn, args);
            return JS_UNDEFINED;
        } else {
            return JSConv<Ret>::to(c, std::apply(fn, args));
        }
    }
};

class JSEngine;

class JSModule {
    friend class JSEngine;

public:
    JSModule& module(const std::string& name) {
        auto& child = children_[name];
        if (!child) child = std::make_unique<JSModule>(name, this);
        return *child;
    }

    template <typename F>
    JSModule& func(const std::string& name, F&& f) {
        addFunc(name, std::forward<F>(f), &std::decay_t<F>::operator());
        return *this;
    }

    /** Callable returns a JS value (e.g. a Promise); argument count must be in [minArgc, maxArgc]. */
    JSModule& funcDynamic(const std::string& name, int minArgc, int maxArgc,
        std::function<JSValue(JSContext*, int, JSValue*)> fn) {
        struct W : FuncBase {
            int mn, mx;
            std::function<JSValue(JSContext*, int, JSValue*)> f;
            W(int a, int b, std::function<JSValue(JSContext*, int, JSValue*)> x)
                : mn(a), mx(b), f(std::move(x)) {}
            int arity() const override { return mx; }
            JSValue call(JSContext* c, int argc, JSValue* argv) override {
                if (argc < mn || argc > mx)
                    return JS_ThrowTypeError(c, "expected %d to %d arguments, got %d", mn, mx, argc);
                return f(c, argc, argv);
            }
        };
        funcs_[name] = std::make_unique<W>(minArgc, maxArgc, std::move(fn));
        return *this;
    }

    template <typename Ret, typename... Args>
    JSModule& func(const std::string& name, Ret(*f)(Args...)) {
        funcs_[name] = std::make_unique<FuncWrap<Ret, Args...>>(std::function<Ret(Args...)>(f));
        return *this;
    }

    template <typename T>
    JSModule& value(const std::string& name, T v) {
        values_[name] = [v](JSContext* c) { return JSConv<decay_t<T>>::to(c, v); };
        return *this;
    }

private:
    std::string name_;
    JSModule* parent_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<JSModule>> children_;
    std::unordered_map<std::string, std::unique_ptr<FuncBase>> funcs_;
    std::unordered_map<std::string, std::function<JSValue(JSContext*)>> values_;

public:
    JSModule(const std::string& name, JSModule* parent) : name_(name), parent_(parent) {}

private:
    template <typename F, typename Ret, typename C, typename... Args>
    void addFunc(const std::string& n, F&& f, Ret(C::*)(Args...) const) {
        funcs_[n] = std::make_unique<FuncWrap<Ret, Args...>>(std::function<Ret(Args...)>(std::forward<F>(f)));
    }
    template <typename F, typename Ret, typename C, typename... Args>
    void addFunc(const std::string& n, F&& f, Ret(C::*)(Args...)) {
        funcs_[n] = std::make_unique<FuncWrap<Ret, Args...>>(std::function<Ret(Args...)>(std::forward<F>(f)));
    }
};

} // namespace qjs
