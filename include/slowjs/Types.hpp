#pragma once
#include <quickjs.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace slowjs {

template <typename T> struct JSConv;

template <> struct JSConv<int> {
    static int from(JSContext* c, JSValue v, bool& ok) {
        int32_t r;
        if (JS_ToInt32(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return (int)r;
    }
    static JSValue to(JSContext* c, int v) { return JS_NewInt32(c, v); }
};

template <> struct JSConv<int64_t> {
    static int64_t from(JSContext* c, JSValue v, bool& ok) {
        int64_t r;
        if (JS_ToInt64(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return r;
    }
    static JSValue to(JSContext* c, int64_t v) { return JS_NewInt64(c, v); }
};

template <> struct JSConv<double> {
    static double from(JSContext* c, JSValue v, bool& ok) {
        double r;
        if (JS_ToFloat64(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return r;
    }
    static JSValue to(JSContext* c, double v) { return JS_NewFloat64(c, v); }
};

template <> struct JSConv<float> {
    static float from(JSContext* c, JSValue v, bool& ok) {
        double r;
        if (JS_ToFloat64(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return (float)r;
    }
    static JSValue to(JSContext* c, float v) { return JS_NewFloat64(c, v); }
};

template <> struct JSConv<bool> {
    static bool from(JSContext* c, JSValue v, bool& ok) {
        if (!JS_IsBool(v)) { ok = false; return false; }
        ok = true;
        return JS_ToBool(c, v);
    }
    static JSValue to(JSContext* c, bool v) { return JS_NewBool(c, v); }
};

template <> struct JSConv<std::string> {
    static std::string from(JSContext* c, JSValue v, bool& ok) {
        const char* s = JS_ToCString(c, v);
        if (!s) { ok = false; return {}; }
        std::string r = s;
        JS_FreeCString(c, s);
        ok = true;
        return r;
    }
    static JSValue to(JSContext* c, const std::string& v) { return JS_NewString(c, v.c_str()); }
};

template <typename T>
struct JSConv<std::vector<T>> {
    static constexpr int64_t MAX_ARRAY_LENGTH = 1000000;

    static std::vector<T> from(JSContext* c, JSValue v, bool& ok) {
        if (!JS_IsArray(c, v)) {
            ok = false;
            JS_ThrowTypeError(c, "expected array");
            return {};
        }
        JSValue lenVal = JS_GetPropertyStr(c, v, "length");
        int64_t len = 0;
        if (JS_ToInt64(c, &len, lenVal) < 0) {
            JS_FreeValue(c, lenVal);
            ok = false;
            return {};
        }
        JS_FreeValue(c, lenVal);

        if (len > MAX_ARRAY_LENGTH) {
            ok = false;
            JS_ThrowRangeError(c, "array length %lld exceeds maximum %lld",
                (long long)len, (long long)MAX_ARRAY_LENGTH);
            return {};
        }

        std::vector<T> result;
        result.reserve((size_t)len);
        for (int64_t i = 0; i < len; i++) {
            JSValue elem = JS_GetPropertyUint32(c, v, (uint32_t)i);
            bool elemOk = false;
            T val = JSConv<T>::from(c, elem, elemOk);
            JS_FreeValue(c, elem);
            if (!elemOk) {
                ok = false;
                JS_ThrowTypeError(c, "array element at index %lld conversion failed", (long long)i);
                return {};
            }
            result.push_back(std::move(val));
        }
        ok = true;
        return result;
    }

    static JSValue to(JSContext* c, const std::vector<T>& v) {
        JSValue arr = JS_NewArray(c);
        if (JS_IsException(arr)) return arr;
        for (size_t i = 0; i < v.size(); i++) {
            JSValue elem = JSConv<T>::to(c, v[i]);
            if (JS_IsException(elem)) {
                JS_FreeValue(c, arr);
                return elem;
            }
            if (JS_SetPropertyUint32(c, arr, (uint32_t)i, elem) < 0) {
                JS_FreeValue(c, arr);
                return JS_EXCEPTION;
            }
        }
        return arr;
    }
};

// Non-owning wrapper for returning a raw JSValue through FuncWrap.
// The wrapped JSValue is DupValue'd on conversion; caller retains original ownership.
struct RawJSValue {
    JSValue val = JS_UNDEFINED;
};

template <> struct JSConv<RawJSValue> {
    static JSValue to(JSContext* c, const RawJSValue& r) {
        return JS_DupValue(c, r.val);
    }
};

template <typename T> using decay_t = std::remove_cv_t<std::remove_reference_t<T>>;
template <typename> struct dependent_false : std::false_type {};

template <typename T>
struct JSConv {
    static_assert(dependent_false<T>::value,
        "JSConv<T> not specialized for this type. Supported: int, int64_t, double, float, bool, std::string, std::vector<T>, RawJSValue");
};

} // namespace slowjs

