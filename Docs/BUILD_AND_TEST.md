# LiveFoot 构建与测试

更新日期：2026-10-13（Asia/Shanghai）。MVP C++ 已实现；最终 Development Editor / Win64 构建退出码 **0**，五项自动化测试 **5 成功、0 失败、0 警告**，测试进程退出码 **0**。已运行实际默认地图的无窗口 Game；**未执行 PIE 画面/交互验收**。

## 1. 实际环境与项目入口

- 项目：`E:/UE/Unreal Projects/LiveFoot/LiveFoot.uproject`；UE 5.6.0，关联 GUID 与 `E:/UE/UnrealEngine-5.6.0-release` 的 Build.version 一致。
- Target：LiveFootEditor / Win64 / Development / x64；模块 LiveFoot，无新增模块依赖。
- 实际编译器：VS 2022，MSVC 14.38.33145（安装目录版本 14.38.33130）；Windows SDK **10.0.22621.0**；引擎 bundled .NET **8.0.300 win-x64**。
- 已加载真实 Blueprint CDO：BP_ThirdPersonCharacter 父类为 `/Script/LiveFoot.LiveFootCharacter`，自动拥有原生 `AnimFootDebug` 组件，AnimationMode 为 AnimationBlueprint；配置 GameMode 的 DefaultPawnClass 匹配该蓝图。
- 实际无窗口运行 `/Game/ThirdPerson/Lvl_ThirdPerson`：玩家 Pawn 为 BP_ThirdPersonCharacter_C，Mesh 为 SKM_Quinn_Simple，AnimInstance 为 ABP_Unarmed_C，左右 foot_l/foot_r 有效。待机高度约 L=8.89、R=8.51 cm，Contact 有效。
- 上层 Git 根为 `E:/UE`，项目源码未跟踪，因此使用 `Saved/Logs/LiveFoot-MVP-baseline.json` 文件哈希检查范围；未初始化仓库或提交父目录。

### 已有问题与处理

| 问题 | 实际状态 |
| --- | --- |
| CombatStateTreeUtility.h 三字段初始化错误 | FaceLocation/TargetPlayerLocation 改为 FVector::ZeroVector，DistanceToTarget 改为 0.f；UHT/构建通过，最新运行未再出现原初始化错误 |
| TP_ThirdPerson 旧父类名称 | 真实蓝图加载和默认地图运行均证明现有重定向有效，无需修改蓝图或配置 |
| DefaultEditor.ini 旧 ThirdPersonCPP 路径、模板显示名 | 未修改；当前默认地图能启动，其余编辑器路径行为待验证 |
| SideScrollingCPP.Build.cs 未被 Target/.uproject 注册 | 未修改或删除，当前 LiveFoot 模块构建通过 |
| 旧日志的跨平台 SDK、Slate/遥测警告 | 没有据此修改引擎；以每次完整日志为准。本次自动化报告没有未预期警告/错误 |

## 2. 实际构建命令与记录

关闭匹配项目的编辑器后执行以下命令，没有使用 Live Coding，也没有清理/重建引擎：

```powershell
& 'E:\UE\UnrealEngine-5.6.0-release\Engine\Build\BatchFiles\Build.bat' `
  LiveFootEditor Win64 Development `
  '-Project=E:\UE\Unreal Projects\LiveFoot\LiveFoot.uproject' `
  -WaitMutex -FromMsBuild -architecture=x64
```

执行时通过 Tee-Object 保存完整输出，并记录 `$LASTEXITCODE`。日志位于项目 `Saved/Logs`：

| 日志 | 真实结果 |
| --- | --- |
| LiveFoot-MVP-build-baseline.log | 退出码 0，Target up to date；这是改动前增量检查 |
| LiveFoot-MVP-build-implementation.log | 退出码 0；UHT 生成反射代码、编译新模块源码并链接 DLL，27.95s |
| LiveFoot-MVP-build-tests.log | 退出码 6；测试文件全局 Flags 名与 Unity 编译中 BrainComponent.h 参数冲突（C4459） |
| LiveFoot-MVP-build-tests-fix.log | 将测试常量改为 LiveFootTestFlags 后退出码 0，6.57s；未改引擎文件或删除功能 |
| LiveFoot-MVP-build-final.log | 最终源码编译/链接退出码 0，6.40s；VS/SDK 如上 |

这是实际项目 Editor 构建，不是从零重建全部引擎。以后反射/默认子对象变更仍应关闭编辑器构建并重新打开；只有本次日志/退出码可作为本次构建证据。

## 3. 实际自动化命令与覆盖

```powershell
& 'E:\UE\UnrealEngine-5.6.0-release\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'E:\UE\Unreal Projects\LiveFoot\LiveFoot.uproject' '/Game/ThirdPerson/Lvl_ThirdPerson' `
  -game -unattended -nullrhi -nosound -nop4 -nosplash -NoLiveCoding `
  '-ExecCmds=Automation RunTests LiveFoot' '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=E:\UE\Unreal Projects\LiveFoot\Saved\Automation\LiveFoot-MVP-final' `
  '-abslog=E:\UE\Unreal Projects\LiveFoot\Saved\Logs\LiveFoot-MVP-automation-final.log' `
  -stdout -FullStdOutLogOutput
```

首轮 `Saved/Automation/LiveFoot-MVP/index.json`：5 项通过，其中组件测试有一次预期缺骨警告和一次测试 World 未 EndPlay 的警告。随后补足测试 World EndPlay、明确预期警告次数并复验。

最终记录（本地 2026-10-13 17:26）：`Saved/Automation/LiveFoot-MVP-final/index.json` / `index.html`；完整日志 `Saved/Logs/LiveFoot-MVP-automation-final.log`，控制台/进程退出码 `LiveFoot-MVP-automation-final-console.log`。5 项全部 Success、0 警告/错误。缺骨/缺动画警告为测试明确预期且各仅一次，不是关闭日志来掩盖故障。

| 测试 | 已自动验证的范围 |
| --- | --- |
| LiveFoot.ProjectAssets | 真实蓝图父类重定向、原生组件继承、Mesh/AnimationMode/AnimClass、双脚骨、GameMode DefaultPawnClass |
| LiveFoot.NumericsAndTiming | 30/60/120 Hz 受控 50 cm/s 世界/切向速度、持续报警时间、静止清除、坡面法线投影；不等于真实画面 FPS 测试 |
| LiveFoot.Boundaries | 首帧、短脉冲、无命中、非静态地面、离地、0/负/过大 dt、恢复、瞬移、深穿透、参数错误、垂直速度进入条件 |
| LiveFoot.ComponentIntegration | 真实 Quinn 姿态/物理射线、自身忽略、真实冻结姿态后平移的滑步正例、骨名回退/缺失/恢复、缺动画恢复、无地面、Movable 地面、诊断关闭和 EndPlay 恢复 Mesh 策略/动画暂停 |
| LiveFoot.LiveMapSmoke | 实际默认地图无窗口 Game 的玩家 Pawn、真实调度的连续最终姿态采样/速度与近地 Contact；不是 PIE 或渲染截图 |

组件集成测试在临时测试 World 同步刷新真实 Mesh 后手动调用诊断 Tick；生产代码不这样强制刷新。LiveMapSmoke 另外验证了生产 Tick/delegate 的真实地图调度。未量化真实渲染帧率、CPU 开销或人体动作诊断准确率。

## 4. 最少手动操作与演示

1. 用关联 UE 5.6.0 重新打开 `LiveFoot.uproject`，打开默认 `Lvl_ThirdPerson`，点击 Play。不需要添加组件或连接任何 Blueprint 节点。
2. 观察脚点/射线/命中法线、屏幕 L/R：绿 CONTACT，青 SWING，红 SUSPECTED SLIP，黄 UNKNOWN。高度单位 cm；总速、世界垂直 vz、切向 tangent 单位 cm/s；无数据/不支持时显示 N/A。
3. 正常待机、WASD 移动/转向/停止、Space 跳跃/落地；确认角色原有行为正常，画面与状态同步。数值按 10 Hz 更新，几何/检测逐帧更新。
4. 正例演示：先平地待机，控制台输入 `LiveFoot.FreezePose 1`，再移动；足部近地但在世界中滑动，超过阈值持续后应显示红色疑似滑步与 TEST 标签。结束立即输入 `LiveFoot.FreezePose 0`，恢复动画；不保存/修改动画资产。
5. 选择蓝图已有的继承 `AnimFootDebug` 组件，在 Details 调整参数即可。屏幕缺骨提示包含 Mesh 名和 LeftFootBone/RightFootBone 属性名。验证硬失败时先设 `bAutoFindFootBones=false`，再填错误骨名；默认自动匹配开启。

控制台：`LiveFoot.Draw 0/1` 关闭/开启全部可视化但保留检测；`LiveFoot.Enabled 0/1` 停止/恢复检测和射线；`LiveFoot.FreezePose 0/1` 为仅开发测试的临时动画暂停。属性 `bDrawDebugVisualization` 是几何和屏幕的总开关，`bShowScreenData` 可单独隐藏文字。控制台变量跨 PIE 保持，测试结束请恢复 1/1/0。

### 实际默认参数

| Details 属性 | 默认值 |
| --- | --- |
| LeftFootBone / RightFootBone、bAutoFindFootBones | foot_l / foot_r、true |
| bEnableFootDiagnostics / bDrawDebugVisualization / bShowScreenData | true / true / true |
| LeftSoleOffset / RightSoleOffset（骨局部，cm） | (0,0,0) / (0,0,0)，保留脚踝位置 |
| GroundTraceChannel | Visibility，忽略 Owner，简单碰撞 |
| Settings.TraceStartOffset / TraceLength（cm） | 20 / 60（从提高后的起点向下总长） |
| ContactHeight / ContactExitHeight（cm） | 18 / 23 |
| ContactVerticalSpeed / ContactExitVerticalSpeed（cm/s） | 35 / 70（绝对世界 Z 速度） |
| ContactConfirmTime / SwingConfirmTime（s） | 0.04 / 0.03 |
| MaxPenetration（cm）/ MaxGroundSlope（deg） | 8 / 55 |
| SlipSpeed / SlipExitSpeed（cm/s） | 15 / 10 |
| SlipDuration / SlipClearTime（s） | 0.10 / 0.05 |
| SmoothingTime / MinDeltaTime / MaxDeltaTime（s） | 0.04 / 0.00001 / 0.25 |
| TeleportDistance（cm/次采样） | 100 |

Quinn 待机默认偏移 0 和接触高度 18 已有效；脚底偏移不是世界 Z 偏移，应按实际脚骨局部轴标定。改偏移之后重新调整高度阈值（Exit > Enter），不要把脚踝约 9 cm 高度误当脚底穿透。参数必须有限、非负、迟滞有序；不合法时明确 Unknown/原因。

## 5. 仍待手动确认的 PIE 验收

- T01/T02：待机持续 ≥10s，前后左右、转向、停止，画面脚点 L/R 与实际足部对应，无状态/文字积累；输入、相机、动画正常。
- T03：跳跃/落地 ≥5 次，Swing 时不报警，落地恢复 Contact。
- T04：坡面、台阶、边缘，检查接触估计和误报；本版为单点近地估计。
- T08：上述冻结姿态正例的红色绘制/文字与画面相符；自动化真实滑动正例已通过，但 PIE 演示画面尚未看过。
- T09-T12：Details 实际调参、错误骨名、射线无命中、暂停恢复/瞬移和平台的屏幕反馈。组件/数学边界已有自动覆盖，UI 操作未验收。
- T13/T14：显示/检测开关无残留，重复 PIE ≥3 次，离开画面再返回；禁用/EndPlay 的 Mesh 恢复已有自动覆盖。
- T15/T16：实际 30/60/120 FPS（可用 t.MaxFPS，记录实际 FPS）、长时 ≥60s 与绘制开关 CPU 开销。受控 30/60/120 Hz 数学结果不冒充实际帧率测试。

本组件每帧每脚最多一次射线，没有 Tick 资产加载或逐帧 UObject 创建。移动/旋转或物理模拟地面不评估滑步；没有法线就不计算切向值。正例/报警属于启发式线索，Foot IK、脚部滚动、尖锐地形与阈值都可能影响结果。

后续每次修改新增真实构建/测试记录（时间、源码快照、命令、退出码、测试编号、参数/实际 FPS、日志/截图、失败原因及剩余事项），保留失败历史，不把未执行项标成通过。

## 6. 本次文件清单与范围检查

新增 C++：

- `Source/LiveFoot/Diagnostics/AnimFootDebugComponent.h`
- `Source/LiveFoot/Diagnostics/AnimFootDebugComponent.cpp`
- `Source/LiveFoot/Diagnostics/AnimFootDiagnosticTypes.h`
- `Source/LiveFoot/Diagnostics/AnimFootDiagnosticModel.h`
- `Source/LiveFoot/Diagnostics/AnimFootDiagnosticModel.cpp`
- `Source/LiveFoot/Tests/AnimFootDiagnosticsTests.cpp`

修改现有 C++：`Source/LiveFoot/LiveFootCharacter.h/.cpp`（原生组件接入）；`Source/LiveFoot/Variant_Combat/AI/CombatStateTreeUtility.h`（三个反射字段显式初始化）。

更新四份文档：`Docs/PRD.md`、`Docs/TECHNICAL_DESIGN.md`、`Docs/IMPLEMENTATION_PLAN.md`、`Docs/BUILD_AND_TEST.md`。根 AGENTS.md 本次已阅读，无需修改。

生成验证记录：`Saved/Logs/LiveFoot-MVP-*`、`Saved/Automation/LiveFoot-MVP*`，以及正常 UBT 生成的 Binaries/Intermediate 文件。最终哈希对比：原有 372 文件中仅上述三个 C++ 文件改变；新增六个 C++ 文件；Content、Config、.uproject、Build.cs、Target 未改动。没有手工修改引擎源码/配置；正常构建工具产生的引擎工具日志不算业务修改。

下一阶段可扩展：平台接触点线/角速度补偿、脚跟/脚尖多点采样、更多指标、批量分析/报告和 Control Rig 修正预览。当前先完成 PIE 手动验收与默认参数标定。
