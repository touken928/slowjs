<h1 align="center">qjs</h1>

<p align="center">
  <strong>Lightweight C++17 bindings for <a href="https://github.com/bellard/quickjs">QuickJS</a>: <code>qjs::JSEngine</code>, module tree, ES modules &amp; bytecode, and <code>qjs::PluginRegistry</code> — headers under <code>include/</code>, namespace <code>qjs::</code>.</strong>
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/c++-17-blue.svg?style=for-the-badge&logo=c%2B%2B" alt="C++17"></a>
  <a href="https://cmake.org/"><img src="https://img.shields.io/badge/cmake-3.16+-064F8C.svg?style=for-the-badge&logo=cmake" alt="CMake 3.16+"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg?style=for-the-badge" alt="License: Apache 2.0"></a>
  <a href="https://github.com/touken928/qjs/stargazers"><img src="https://img.shields.io/github/stars/touken928/qjs?style=for-the-badge&color=yellow&logo=github" alt="GitHub stars"></a>
</p>

<p align="center">
  <a href="README_zh.md">简体中文</a>
</p>

---

Main headers: **`js_engine.h`**, **`js_module.h`**, **`js_plugin.h`**, **`js_types.h`**.

## CMake: link `qjs` into your project

With **`add_subdirectory`** (submodule, vendor copy, etc.):

```cmake
add_subdirectory(third_party/qjs)   # adjust path

add_executable(myapp main.cc)
target_link_libraries(myapp PRIVATE qjs::qjs)
```

Or **`FetchContent`**:

```cmake
include(FetchContent)
FetchContent_Declare(qjs
    GIT_REPOSITORY https://github.com/touken928/qjs.git
    GIT_TAG        main)
FetchContent_MakeAvailable(qjs)

add_executable(myapp main.cc)
target_link_libraries(myapp PRIVATE qjs::qjs)
```

Linking **`qjs::qjs`** pulls in **`include/`** and **PUBLIC**-ly propagates **`quickjs`**. The QuickJS engine is fetched via **`cmake/qjs_quickjs.cmake`** using **`QJS_QUICKJS_REV`** (requires network access to GitHub on first configure).

---

## Examples

### 1. Engine lifecycle + run an ES module

```cpp
#include <js_engine.h>

int main() {
    qjs::JSEngine engine;
    engine.initialize();

    bool ok = engine.runModuleCode("hello.js", R"(
export const msg = "hello";
)");
    (void)ok;

    engine.cleanup();
    return 0;
}
```

### 2. Register C++ functions on the root module, then `import` from JS

Register functions, call **`installModules()`**, then use them inside **`runModuleCode`**:

```cpp
#include <js_engine.h>

int main() {
    qjs::JSEngine engine;
    engine.initialize();

    engine.root().module("native").func("add", [](int a, int b) {
        return a + b;
    });
    engine.installModules();

    engine.runModuleCode("app.js", R"(
import { add } from 'native';
export const sum = add(40, 2);
)");

    engine.cleanup();
    return 0;
}
```

### 3. Plugins: implement `IEnginePlugin` and `install`

```cpp
#include <js_engine.h>
#include <js_plugin.h>
#include <string>

struct DemoPlugin : qjs::IEnginePlugin {
    const char* name() const override { return "demo"; }
    void install(qjs::JSEngine&, qjs::JSModule& root) override {
        root.module("demo").value("label", std::string("qjs"));
    }
};

int main() {
    qjs::JSEngine engine;
    qjs::PluginRegistry plugins;
    plugins.emplace<DemoPlugin>();

    engine.initialize();
    plugins.installAll(engine, engine.root());
    engine.installModules();

    engine.runModuleCode("main.js", R"(
import { label } from 'demo';
export const out = label;
)");

    engine.cleanup();
    return 0;
}
```

---

## Build & tests (standalone)

From a **clone of this repo** (do not nest the build directory under an unrelated parent’s build tree):

```bash
git clone https://github.com/touken928/qjs.git
cd qjs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

When this project is the **top-level** CMake project, **`QJS_BUILD_TESTS`** defaults to **`ON`** (builds **`qjs_tests`** and fetches GoogleTest if **`GTest::gtest_main`** is not already available). When **`qjs`** is embedded via **`add_subdirectory`**, it defaults to **`OFF`**.

To bump QuickJS: set cache variable **`QJS_QUICKJS_REV`** (see **`cmake/qjs_quickjs.cmake`**) or pass **`-DQJS_QUICKJS_REV=...`** at configure time.

### CMake targets

| Target | Role |
|--------|------|
| **`quickjs`** / **`qjs::quickjs`** | QuickJS engine static library (fetched sources + generated headers) |
| **`qjs`** / **`qjs::qjs`** | C++ wrapper (`qjs::JSEngine`, etc.), **PUBLIC** depends on **`quickjs`** |

Downstream projects only need **`target_link_libraries(your_target PRIVATE qjs::qjs)`**; include paths and **`quickjs`** propagate via **PUBLIC** / **INTERFACE** — no global **`include_directories`**.

Optional tests: with **`QJS_BUILD_TESTS=ON`** at the top level, **`cmake/qjs_tests.cmake`** adds the **`qjs_tests`** executable.

## Repositories

- **This wrapper** — [github.com/touken928/qjs](https://github.com/touken928/qjs) (submodule or standalone clone).
- **QuickJS engine** — [github.com/bellard/quickjs](https://github.com/bellard/quickjs)

---

## License

The **CMake scripts, C++ bindings, and headers** maintained in this repository are released under the [**Apache License 2.0**](https://www.apache.org/licenses/LICENSE-2.0.txt). The full text is in [`LICENSE`](LICENSE).

QuickJS engine sources fetched at build time are governed by [bellard/quickjs](https://github.com/bellard/quickjs) (MIT). That upstream license applies to the engine; it is not replaced by the Apache-2.0 terms for this wrapper.
