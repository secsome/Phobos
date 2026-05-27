# Mission: 整理 OwnerDraw / WWUI 代码结构

## 目标

整理并重构当前 OwnerDraw / WWUI 相关代码，使其结构更清晰、职责更明确，并为后续维护和继续还原游戏 UI 逻辑打好基础。本任务以代码组织和类型设计清理为主，应尽量保持现有行为不变。

## 背景

此前还原得到的 OwnerDraw / WWUI 逻辑主要集中在以下文件中：

- `YRpp/OwnerDraw.h`
- `src/Render/WWUI.h`
- `src/Render/WWUI.cpp`

目前这些文件承载了过多职责：OwnerDraw 数据结构、控件状态访问、消息处理、Hook 后实现和控件级辅助逻辑混在一起。尤其是 `OwnerDrawDialogElement` 已经承担了所有控件的通用数据与特定控件数据访问，导致后续代码需要依赖辅助函数或不直观的字段读取。

本轮任务需要将这些内容整理为更合理的结构，而不是继续堆叠到 `WWUI.cpp/h` 中。

## 清理范围

重点清理以下文件和相关引用：

- `YRpp/OwnerDraw.h`
- `src/Render/WWUI.h`
- `src/Render/WWUI.cpp`

如需拆分文件，应新增 `src/OwnerDraw/` 目录，并同步更新 `Phobos.vcxproj` 中对应的 `<ClCompile>` 与 `<ClInclude>` 项。

## 设计要求

### `YRpp/OwnerDraw.h`

1. 重新整理 `OwnerDrawDialogElement`，避免它继续作为所有控件字段的混杂容器。
2. 保留必要的通用字段，但将控件特定数据拆分为更明确的结构体。
3. 为不同控件提供 `AsXXX` 风格的转换接口，例如 `AsScrollBar()`、`AsListBox()`、`AsComboBox()` 等。
4. 后续代码应优先通过具体控件结构体成员读取数据，而不是依赖辅助函数间接读取。
5. 拆分或补充结构体时，必须保持与游戏原始内存布局一致。对不确定字段应保留清晰命名和注释，避免随意改动偏移。

### OwnerDraw 实现拆分

1. 将 OwnerDraw 控件和消息处理逻辑从 `src/Render/WWUI.cpp/h` 中拆出。
2. 新建 `src/OwnerDraw/` 目录承载这些实现。
3. 每一种控件的主要逻辑尽量放在独立的 `.cpp` 文件中，避免继续形成超大实现文件。
4. 对外暴露接口放在统一的 `src/OwnerDraw/OwnerDraw.h` 中。
5. 文件内本地辅助函数使用 `static`，不要使用匿名 namespace。
6. 拆分后 `src/Render/WWUI.cpp/h` 只保留真正属于 Render/WWUI 层的内容，或作为兼容入口转发到新的 OwnerDraw 接口。

## 推荐目录结构

可按实际代码情况调整，但整体方向应接近：

```text
src/OwnerDraw/
├── OwnerDraw.h
├── OwnerDraw.cpp
├── OwnerDraw.Hooks.cpp
├── ScrollBar.cpp
├── ListBox.cpp
├── ComboBox.cpp
├── Slider.cpp
├── Progress.cpp
├── Edit.cpp
├── Static.cpp
├── Tab.cpp
├── Button.cpp
└── Input.cpp
```

如果某些控件逻辑很小，可以合并到同类文件中，例如多个 Button 变体可暂时放入 `Button.cpp`。拆分标准应以职责清楚、便于后续维护为准，而不是机械地制造过多文件。

## 执行原则

1. 优先整理结构和职责边界，不主动改变游戏行为。
2. 每次移动或重命名代码后，确认引用、Hook、声明和项目文件同步更新。
3. 拆分过程中保留现有调用路径的兼容性，避免一次性大范围破坏外部接口。
4. 对布局敏感的 YRpp 结构体，必须谨慎处理字段顺序、大小和对齐。
5. 如果发现现有字段命名、类型或偏移明显错误，可以一并修正，但需要说明依据。
6. 不引入与本次整理无关的新功能。
7. 不使用 `goto`，不留下临时调试代码。

## 建议执行流程

1. 阅读当前 `YRpp/OwnerDraw.h`、`src/Render/WWUI.h`、`src/Render/WWUI.cpp` 和相关 Hook 文件，梳理现有类型、函数和调用关系。
2. 确认哪些内容应留在 Render/WWUI，哪些内容应迁移到 `src/OwnerDraw/`。
3. 先整理 `YRpp/OwnerDraw.h` 的结构体和 `AsXXX` 接口，确保布局仍能解释现有代码。
4. 创建 `src/OwnerDraw/OwnerDraw.h`，定义对 Phobos 内部暴露的 OwnerDraw 接口。
5. 按控件类型逐步拆分实现文件，并同步更新 include、命名和项目文件。
6. 清理 `src/Render/WWUI.cpp/h` 中已经迁出的内容，只保留必要入口或兼容层。
7. 自审是否还存在重复辅助函数、无法追溯的硬编码、过大的单文件实现或不清晰的控件字段访问。
8. 使用 Debug 配置构建验证，推荐命令为 `scripts\build_debug.bat`。

## 验收标准

任务完成时应满足以下条件：

- `OwnerDrawDialogElement` 已按控件职责拆分，调用方可以通过 `AsXXX` 访问具体控件数据。
- OwnerDraw 控件逻辑已从 `src/Render/WWUI.cpp/h` 中合理迁移到 `src/OwnerDraw/`。
- 每类控件的主要实现位于独立或职责相近的 `.cpp` 文件中。
- 对外接口集中在 `src/OwnerDraw/OwnerDraw.h`。
- 本地辅助函数使用 `static`，没有新增匿名 namespace。
- `Phobos.vcxproj` 已包含所有新增或迁移后的源文件和头文件。
- Debug 构建通过。
- 重构前后的行为应保持一致；若存在行为变化，必须明确说明原因和影响范围。
