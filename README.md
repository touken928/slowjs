# qjs

基于 [QuickJS](https://github.com/bellard/quickjs) 的 **C++17** 封装：引擎 **`qjs::JSEngine`**、模块树 **`qjs::JSModule`**、ES 模块与字节码、插件 **`qjs::PluginRegistry`**。头文件在 **`include/`**，命名空间 **`qjs::`**。

主要头文件：**`js_engine.h`**、**`js_module.h`**、**`js_plugin.h`**、**`js_types.h`**。

---

## CMake：把 `qjs` 链进工程

用 **`add_subdirectory`**（子模块、拷贝进 `third_party` 等）：

```cmake
add_subdirectory(third_party/qjs)   # 路径按你的仓库调整

add_executable(myapp main.cc)
target_link_libraries(myapp PRIVATE qjs::qjs)
```

或用 **`FetchContent`**（声明里换成你的 URL / 路径与 tag）：

```cmake
include(FetchContent)
FetchContent_Declare(qjs
    GIT_REPOSITORY https://github.com/touken928/qjs.git
    GIT_TAG        main)
FetchContent_MakeAvailable(qjs)

add_executable(myapp main.cc)
target_link_libraries(myapp PRIVATE qjs::qjs)
```

链接 **`qjs::qjs`** 会带上 **`include/`**。QuickJS 由 **`cmake/quickjs.cmake`** 里 **`QJS_QUICKJS_REV`** 经 FetchContent 获取（首次配置需能访问 GitHub）。

---

## 代码示例

### 1. 引擎生命周期 + 执行 ES 模块

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

### 2. 在根模块上注册 C++ 函数，再给 JS 用

先挂函数，再 **`installModules()`**，最后 **`runModuleCode`** 里 `import` 该模块：

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

### 3. 插件：实现 `IEnginePlugin`，统一 `install`

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

## 单独编译 / 跑本库测试

在 **`qjs` 仓库根目录**（本目录）建构建目录，勿放在上层工程根下：

```bash
cd third_party/qjs   # 或独立克隆的 qjs 根目录
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

此处 **`cmake` 以本目录为顶层工程**，选项 **`QJS_BUILD_TESTS`** 默认为 **`ON`**，会构建 **`qjs_tests`** 并拉取 GoogleTest（若工程里尚未提供 **`GTest::gtest_main`**）。把 **`qjs` 嵌进别的工程时**，该选项默认为 **`OFF`**。

升级 QuickJS：改 **`cmake/quickjs.cmake`** 中的 **`QJS_QUICKJS_REV`**。

## 仓库与依赖

- **qjs（本封装）**：[github.com/touken928/qjs](https://github.com/touken928/qjs) — 可与本目录作为子模块或独立克隆对应。
- **QuickJS（底层引擎）**：[github.com/bellard/quickjs](https://github.com/bellard/quickjs)

---

## 许可证

本仓库中由维护者持有的 **CMake / C++ 封装与头文件** 以 [**Apache License 2.0**](https://www.apache.org/licenses/LICENSE-2.0.txt) 发布，**完整许可证全文**见根目录 [`LICENSE`](LICENSE)；版权与第三方说明见 [`NOTICE`](NOTICE)。

通过 FetchContent 获取的 **QuickJS 引擎源码** 遵循 [bellard/quickjs](https://github.com/bellard/quickjs) 的许可（MIT），不以本仓库的 Apache-2.0 替代。
