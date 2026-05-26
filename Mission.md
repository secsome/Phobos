# Mission: 反编译并替换 Win32Dialog / OwnerDraw UI 逻辑

## 目标

基于 `gamemd.i64` 中已经定位到的 Win32 UI 与 OwnerDraw 相关函数，使用 IDA 逐个还原等价的 C++ 实现，并通过 Phobos Hook 替换原始函数。还原过程中可以借助 Hex-Rays 辅助理解，但关键行为必须能由反汇编验证。最终目标是让这些非游戏过程界面中的控件绘制与消息处理逻辑能够以仓库内的 C++ 代码维护，同时保持与游戏原始行为一致。

## 背景

游戏使用 Win32 UI + OwnerDraw 实现了一套界面系统，主要用于菜单、对话框、列表、按钮、输入框等非战斗过程中的 UI 交互与显示。目前控件相关的自定义窗口过程和消息处理函数已经在 `gamemd.i64` 中定位。任务重点不是重新设计 UI，而是从汇编还原原始逻辑，并在 Phobos 中建立可维护的替代实现。

## 硬性约束

1. 使用 IDA 分析时，可以使用 Hex-Rays 辅助梳理控制流和生成初稿，但不能盲信反编译结果。关键判断仍需能够追溯到反汇编、交叉引用、数据引用、调用约定、寄存器和栈状态。
2. 如果基于 Hex-Rays 结果完成还原后，用户在验证或使用中遇到问题，必须回到反汇编层面验证还原逻辑的正确性，并据此修正实现。
3. 数据库中已有的结构体、枚举和符号只能作为参考，不默认视为绝对正确；发现不准确时，需要记录并在必要时修正。
4. 每轮只处理一个目标函数。完成当前函数的分析、C++ 还原、Hook 替换、构建验证和审查之后，才能进入下一个函数。
5. 还原后的 C++ 代码不能包含 `goto`。应使用清晰的分支、循环和辅助函数表达原始控制流。
6. C++ 行为必须尽可能等价于游戏原始逻辑。除非替换函数本身需要，否则不要主动改造其与仓库其它系统的交互。
7. 如果分析过程中遇到未知的 `uMsg`、控件消息或硬编码分支，可以根据上下文、Win32 消息语义、资源定义和交叉引用自行推断并补全，但必须记录推断依据和不确定性。
8. 如果目标函数调用了其它 OwnerDraw / WWUI 相关函数，应继续还原被调用逻辑；不要只用函数指针或原始地址转发来绕过实现。
9. 每个函数完成后，必须先交由用户确认。用户确认无误后，再创建对应的本地 git 提交；不要推送到远端。
10. 使用完 IDA 后，应保存数据库以保留分析成果；不再使用时，应关闭数据库。

## 代码放置

所有还原出的声明和实现放在以下文件中：

- `src/Render/WWUI.h`
- `src/Render/WWUI.cpp`

用于替换原始函数的 Hook 放在：

- `src/Render/WWUI.Hooks.cpp`

如需新增文件，必须同步更新 `Phobos.vcxproj` 中对应的 `<ClCompile>` 或 `<ClInclude>` 项。

## YRpp 定义补充

遇到需要补充的游戏定义，或需要引用游戏原始内容时，应在 `YRpp/` 的对应位置补充定义，而不是在 Phobos 代码中散落硬编码。

- 对游戏全局变量、常量地址、数组、字符串或对象引用，使用 YRpp 现有风格和合适的宏进行定义，例如 `DEFINE_REFERENCE`、`DEFINE_POINTER`、`DEFINE_ARRAY_REFERENCE` 等。
- 对缺失或不准确的结构体、类、枚举、成员函数、虚函数、资源 ID、窗口消息常量等，应在 YRpp 中补充或修正。
- 修改 YRpp 时要意识到它是子模块。相关变更需要作为独立变更维护，并在 Phobos 中正确处理子模块指针。

## 外部资料与 Ares 兼容

游戏资源文件位于：

- `E:\SteamLibrary\steamapps\common\Command & Conquer Red Alert II\gamemd.rc`

遇到游戏内硬编码 ID、对话框 ID、控件 ID、菜单 ID、字符串 ID 或其它资源相关常量时，应参考该 `gamemd.rc`，并在 YRpp 中补充对应的枚举或常量定义。

Ares 可能修改了相关 UI / OwnerDraw 逻辑。还原后的实现应覆盖以下两种场景：

- Ares 不存在时，保持游戏原始逻辑。
- Ares 存在时，包含 Ares 对相关逻辑的修改或兼容分支。

Ares 相关文件位于：

- `E:\SteamLibrary\steamapps\common\Command & Conquer Red Alert II\Ares.dll.inj`
- `E:\SteamLibrary\steamapps\common\Command & Conquer Red Alert II\Ares.dll`

可使用 IDA-MCP 对 `Ares.dll` 进行分析。`Ares.dll.inj` 描述了 Syringe Hook 定义，可用于定位 Ares 修改过的 Hook 点和目标函数。

`.inj` 文件中的 Hook 语义参考 `DEFINE_HOOK`：

- Hook 定义包含 Hook 地址、跳转到的函数名称以及覆盖字节数。
- 覆盖字节数对应 `DEFINE_HOOK(address, HookName, size)` 中的 `size`，表示被 Hook 覆盖并需要处理的原始指令长度。
- 跳转目标函数的返回值表示函数执行完后继续执行的地址。
- 返回 `0` 是特例，表示由 Syringe 自动还原覆盖的原始字节，然后从 Hook 点之后继续执行。

## 每轮执行流程

对每一个目标函数按以下流程推进：

1. 从 IDA 反汇编出发，必要时结合 Hex-Rays 辅助理解控制流，整理函数入口、参数来源、返回值、寄存器使用、栈布局、关键分支、调用目标、全局变量和数据结构访问。
2. 识别所有 `uMsg`、控件消息、资源 ID 和硬编码常量；能确认的写入 YRpp 定义，不能直接确认的根据上下文推断并记录依据。
3. 检查目标函数是否调用其它 OwnerDraw / WWUI 相关函数；若调用，应将这些被调用逻辑纳入当前还原范围或建立明确的后续拆分计划。
4. 检查 Ares 的 `.inj` 和 `Ares.dll`，确认 Ares 是否修改了当前函数、相关 Hook 点或被调用逻辑；必要时同时还原 Ares 存在和不存在两种路径。
5. 必要时动态维护分析文档，记录地址、消息分支、结构体假设、资源 ID 来源、Ares 差异、未确认点和验证结论。
6. 如任务复杂或需要并行交叉检查，可创建必要的 subagent 辅助分析、审查或对照反汇编。
7. 将函数还原为无 `goto` 的 C++ 代码，放入 `src/Render/WWUI.h` 与 `src/Render/WWUI.cpp`。
8. 在 `src/Render/WWUI.Hooks.cpp` 中添加 Hook，用还原后的 C++ 实现替换原始函数。
9. 自审实现与反汇编的一致性，重点检查调用约定、返回值、消息分发、资源释放、句柄有效性、默认处理路径、Ares 兼容分支和边界条件。
10. 使用 Debug 配置构建验证。推荐命令为 `scripts\build_debug.bat`。
11. 向用户报告本轮完成内容、验证结果、残余风险和需要人工确认的点。
12. 等用户确认后，为当前函数创建一个本地 git 提交；不要 push。

## 完成标准

任务整体完成时应满足以下条件：

- 下方所有目标函数均已逐个分析、还原、Hook 替换并通过 Debug 构建。
- 每个函数都有清晰的分析记录，能够追溯关键分支和行为来源。
- 对使用 Hex-Rays 辅助还原的函数，关键行为仍能回溯到反汇编依据；若用户反馈问题，已通过反汇编重新验证并修正。
- 未知 `uMsg`、资源 ID、硬编码常量和 OwnerDraw 相关调用均已确认或记录了合理推断依据。
- 需要补充的游戏定义已放入 YRpp 的对应位置，未在 Phobos 代码中散落无法追溯的硬编码。
- Ares 存在和不存在时的行为差异已分析并体现在实现中。
- 还原代码位于指定文件中，Hook 位于指定 Hook 文件中。
- 每个函数完成后均经过用户确认，并创建了对应的本地 git 提交。
- IDA 数据库已保存，未继续使用时已关闭。

## 替换目标

按顺序逐个处理以下函数：

1. `OwnerDraw_StandardWndProc` - 大多数游戏内菜单会优先调用的窗口处理函数，之后才进入其它特殊处理函数。
2. `OwnerDraw_WindowProc` - 大多数游戏内控件的自定义消息处理函数，会调用每个控件的特殊处理，并负责消息重入等逻辑。
3. `WWUI::ScrollBarCtrl` - `ScrollBar` 的自定义消息处理。
4. `WWUI::ListBoxCtrl` - `ListBox` 的自定义消息处理。
5. `WWUI::ComboBoxCtrl` - `ComboBox` 的自定义消息处理。
6. `WWUI::SliderCtrl` - `msctls_trackbar32` 的自定义消息处理。
7. `WWUI::ProgressCtrl` - `msctls_progress32` 的自定义消息处理。
8. `WWUI::NewEditCtrl` - `NewEdit` 的自定义消息处理。
9. `WWUI::EditCtrl` - `Edit` 的自定义消息处理。
10. `WWUI::StaticCtrl` - `Static` 的自定义消息处理。
11. `WWUI::TabCtrl` - `SysTabControl32` 的自定义消息处理。
12. `WWUI::GroupBoxCtrl` - `BS_GROUP` 样式 `Button` 的自定义消息处理。
13. `WWUI::OwnerDrawCtrl` - `BS_OWNERDRAW` 样式 `Button` 的自定义消息处理。
14. `WWUI::CheckboxCtrl` - `BS_AUTOCHECKBOX` 样式 `Button` 的自定义消息处理。
15. `WWUI::RadioCtrl` - `BS_AUTORADIOBUTTON` 样式 `Button` 的自定义消息处理。
16. `WWUI::InputCtrl` - `msctls_hotkey32` 的自定义消息处理。
17. `WWUI::InputCtrlColor` - 剩余某类控件的自定义消息处理，需在分析中确认具体控件类型。
