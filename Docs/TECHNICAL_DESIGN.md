# LiveFoot 技术设计（MVP）

更新日期：2026-10-13。状态：已实现并通过 UHT、Editor 构建及五项自动化测试。已执行真实默认地图无窗口 Game 运行；未执行 PIE 画面验收。以本节新增的实际状态和源码为准。

## 1. 事实基线与证据等级

| 项目 | 已检查内容 | 结论/限制 |
| --- | --- | --- |
| `.uproject` | `EngineAssociation={CAE52CCA-4C81-CD28-1127-A181C55F3724}` | 该 GUID 在 `HKCU/Software/Epic Games/Unreal Engine/Builds` 指向 `E:/UE/UnrealEngine-5.6.0-release` |
| 实际版本 | 引擎 `Engine/Build/Build.version`、`Binaries/Win64/LiveFootEditor.target`、`Saved/Logs/LiveFoot.log` | UE 5.6.0，日志版本 `5.6.0-0+UE5`；CL=0 不提供准确源码提交标识 |
| Target | `Source/LiveFoot.Target.cs`、`Source/LiveFootEditor.Target.cs` | Game / Editor；`BuildSettingsVersion.V5`；`EngineIncludeOrderVersion.Unreal5_6`；都只添加 `LiveFoot` |
| 主模块 | `Source/LiveFoot/LiveFoot.cpp`、`LiveFoot.Build.cs` | `IMPLEMENT_PRIMARY_GAME_MODULE`；Runtime，Default 加载 |
| 依赖 | `LiveFoot.Build.cs` | Core、CoreUObject、Engine、InputCore、EnhancedInput、AIModule、StateTreeModule、GameplayStateTreeModule、UMG；无私有依赖 |
| 插件 | `.uproject` 显式列表 | ModelingToolsEditorMode（Editor）、StateTree、GameplayStateTree；这不是所有实际启用插件的完整枚举 |
| 角色 | `LiveFootCharacter.h/.cpp` | `ALiveFootCharacter : ACharacter`，abstract；已有相机/移动/Enhanced Input；Mesh/Anim Blueprint 由派生蓝图指定；已通过 CreateDefaultSubobject 接入 UAnimFootDebugComponent |
| 关卡入口 | `Config/DefaultEngine.ini` | 启动/游戏地图 `/Game/ThirdPerson/Lvl_ThirdPerson`；全局 GameMode `/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode`；实际默认地图运行 Pawn 已确认是 BP_ThirdPersonCharacter_C，覆盖配置的最终效果已在无窗口 Game 验证 |
| 已有构建文件 | `Binaries/Win64` | DLL/PDB/modules/target 存在，收据为 Editor / Win64 / Development / x64；本次源码重新编译/链接通过，详见构建日志 |

目录概况：`Source/LiveFoot` 包含主角色、GameMode、PlayerController，以及 `Variant_Combat`、`Variant_Platforming`、`Variant_SideScrolling`。三个变体角色直接继承 ACharacter，不是 ALiveFootCharacter 子类，MVP 不自动接入变体。`Content` 包含 ThirdPerson、Characters/Mannequins、Input、LevelPrototyping 及对应变体资产；`Binaries/Intermediate/Saved/DerivedDataCache` 为现有生成目录。

额外的 `Source/LiveFoot/Variant_SideScrolling/SideScrollingCPP.Build.cs` 声明 SideScrollingCPP 规则，但当前 `.uproject` 和两个 Target 没有引用该独立模块。将其视为待核实的模板残留，不删除、不新增模块注册。变体代码仍位于 LiveFoot 源码树内，不能因 MVP 不使用其玩法而忽略编译问题。

## 2. 蓝图、Mesh 与动画调查

初始调查仅有二进制引用线索。本次 LiveFoot.ProjectAssets 已加载真实 Blueprint CDO，确认原生父类重定向、组件继承、网格与动画类；LiveFoot.LiveMapSmoke 验证默认地图的实际玩家角色。动画图中 Foot IK/Root Motion 的具体启用设置及画面效果仍待编辑器检查。

| 资产 | 可读引用证据 | 编码前需要确认 |
| --- | --- | --- |
| `/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode` | BP_ThirdPersonCharacter、BP_ThirdPersonPlayerController；旧父类 TP_ThirdPersonGameMode | 实际 DefaultPawnClass、PlayerControllerClass，以及地图 GameMode 覆盖 |
| `/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter` | SKM_Quinn_Simple、ABP_Unarmed、旧父类 `/Script/TP_ThirdPerson.TP_ThirdPersonCharacter` | 实际父类是否经重定向为 ALiveFootCharacter；Mesh、AnimClass、AnimationMode、组件偏移/缩放 |
| `/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple` | SK_Mannequin、PA_Mannequin；foot_l、foot_r、ball_l、ball_r | 运行中骨骼索引、骨变换、LOD 保留和脚底位置 |
| `/Game/Characters/Mannequins/Meshes/SK_Mannequin` | 资产类型为 Skeleton，foot_l、foot_r | 不因 `SK_` 前缀误认成 Skeletal Mesh |
| `/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed` | SK_Mannequin、BS_Idle_Walk_Run、Idle/Jump/Fall/Land、CR_Mannequin_FootIK、Control Rig 节点名称 | 实际状态机、Foot IK 是否启用、Root Motion 模式、最终姿态时序 |

`DefaultEngine.ini` 已配置 TP_ThirdPerson → /Script/LiveFoot 的 GameName 重定向，以及角色/GameMode/Controller 的 Class 重定向。本次真实 Blueprint CDO 和默认地图运行已确认角色父类解析为 ALiveFootCharacter；不是蓝图损坏。`SKM_Manny_Simple` 也存在，但当前角色引用线索指向 Quinn。

## 3. C++ 类型与接入

继续使用现有 LiveFoot Runtime 模块，不新增插件、Slate、ControlRig 或第三方依赖。诊断仅使用现有 Core/CoreUObject/Engine 能力，编译已验证；Build.cs、Target 和 .uproject 均未修改。

已新增至 `Source/LiveFoot/Diagnostics/`：

- `AnimFootDiagnosticTypes.h`：`EAnimFootPhase { Unknown, Contact, Swing }`、`EAnimFootIssue`、`FAnimFootDiagnosticSettings`、`FAnimFootDebugResult`（有效性/原因、骨位置、脚底采样点、射线命中、间距、速度、状态、报警/适用性）。
- `AnimFootDebugComponent.h/.cpp`：`UAnimFootDebugComponent : UActorComponent`，负责初始化、Tick、采样、状态更新、绘制和清理。新增跨类使用的 UCLASS 使用 `LIVEFOOT_API`，反射声明保持 UHT 合规。
- `AnimFootDiagnosticModel.h/.cpp`：无 UObject 的数值/状态逻辑；组件每脚私有历史：前一位置/时间、接触候选计时、平滑速度、滑步计时、上一 Mesh/配置标识；不将全部历史暴露为反射属性。

ALiveFootCharacter 构造函数已创建 AnimFootDebug 默认子对象，通过 TObjectPtr 属性持有；组件在 BeginPlay 初始化消息 key，随后在 Tick 从所属 ACharacter 的 GetMesh 获取目标 Mesh。不在构造函数读取动画姿态，不依赖新增蓝图节点，不重写输入或 GameMode，不在 C++ 硬编码资产路径。

真实蓝图 CDO 及运行 Pawn 均已验证继承 AnimFootDebug 子对象。添加反射类/子对象后必须关闭编辑器做完整 Editor 构建，再重新打开确认；不能用 Live Coding 成功替代继承组件验证。

## 4. 每帧数据流程与有效性

顺序：检查对象/参数/姿态与历史 → 读取双脚变换 → 射线与间距 → 计算有效世界速度 → 接触状态 → 切向速度与滑步 → 提交结果与绘制。左右脚独立；接触只限制垂直速度，不限制切向速度，以免滑动中的脚被排除为 Swing。

实际调度：诊断 Tick 使用 TG_PostUpdateWork，添加 Mesh 的 Tick prerequisite，订阅 RegisterOnBoneTransformsFinalizedDelegate，并要求自上次采样后发生新的 Finalize、且无正在运行的并行评估。无新姿态则输出 PoseNotReady，不读取陈旧姿态。实际默认地图无窗口运行已证明该调度能产生有效连续样本；调试点与画面的逐帧对齐仍待手动确认。只在游戏线程读取 UObject，不强制调用引擎私有刷新函数。

诊断启用时临时设置目标 Mesh 为 `AlwaysTickPoseAndRefreshBones` 并禁用 Update Rate Optimization，保存原值，关闭检测/EndPlay/切换 Mesh 时恢复。这样限制为一个受测角色的运行时设置，不保存蓝图资产；禁用/EndPlay 恢复已通过组件集成测试，实际开销尚未测量。不能用“骨骼位置没变”认定姿态陈旧，待机也可不动。

Mesh 未注册、无动画实例、骨索引无效/变换缓冲未就绪、非有限值均拒绝采样。先用 GetBoneIndex 验证存在，再使用已缓存索引读取世界变换；不以原点/单位变换或 GetSocketTransform 的 fallback 当作有效脚。默认名为 `foot_l` / `foot_r`，改名或 Mesh 切换时重新校验并清空历史。

## 5. 地面与接触

定义 `P` 为骨原点世界位置，`S` 为脚底采样点：把每脚可配置的骨局部 `LeftSoleOffset` / `RightSoleOffset` 经骨世界变换转换。偏移默认为 0；足骨原点不等于鞋底。无窗口测试中 Quinn 待机脚踝高度约 L=8.91、R=8.55 cm，18 cm 初始阈值可进入 Contact；完整运动标定仍待 PIE。

世界上方向 `U=(0,0,1)`。射线起点 `Start=S + U*Settings.TraceStartOffset`，终点 `Start - U*Settings.TraceLength`；默认偏移 20 cm、从起点向下总长度 60 cm。使用可配置碰撞通道（初选 Visibility）与简单碰撞，忽略 Owner；不新增项目碰撞通道。FCollisionQueryParams 构造与 Owner 忽略已编译并在真实碰撞测试中验证；默认地图 Visibility 命中可用。

有效命中要求 blocking hit、非起点穿透、有效法线和满足 `dot(N,U) >= cos(Settings.MaxGroundSlope)`。地面间距 `h=dot(S-HitPoint,N)`，不是起点到命中点的 Hit.Distance。过深穿透、无效法线或不可支持表面不能被当作正常 Contact。

状态机（全部按秒计时）：

1. 数据无效 → Unknown，重置状态/速度/报警历史。
2. 初次有效样本：播种历史并显示 Swing / Warmup，速度与滑步为 N/A；后续连续样本才能确认 Contact。
3. Swing → Contact：有效地面、`-Settings.MaxPenetration <= h <= Settings.ContactHeight`，且脚底世界 Z 速度绝对值不超过 Settings.ContactVerticalSpeed，持续 Settings.ContactConfirmTime。
4. Contact → Swing：无命中、坡度不合格、`h > Settings.ContactExitHeight` 或垂直速度绝对值超过 Settings.ContactExitVerticalSpeed，持续 Settings.SwingConfirmTime；严重穿透直接 Unknown。
5. 必须 ContactExitHeight > ContactHeight。退出等待期间只保持相位；没有合格新命中、高度/垂直速度已超退出阈值即停止滑步累计，不沿用旧命中。

CharacterMovement::IsFalling 时直接输出 Swing 并清除滑步。垂直速度参与接触判断，切向速度不作为进入接触的排除条件。该算法估计近地接触，不能识别精确载荷或 heel/toe roll。跳跃/落地、坡边和台阶是必测误报场景；本版不读取动画曲线、Notify 或 IK 骨作为接触真值。

## 6. 速度与疑似滑步

对连续有效样本、实际采样间隔 `dt`：骨世界速度 `Vbone=(P_t-P_prev)/dt`；脚底采样点速度 `Vsole=(S_t-S_prev)/dt`。屏幕显示 `|Vbone|`；在合格地面上取 `Vtan=Vsole-dot(Vsole,N)*N`，滑步指标为 `|Vtan|`（cm/s）。**不减角色根部速度**：静态地面的支撑脚应在世界空间近似固定，减角色速度会把正常支撑误判为滑动。

可选指数平滑 `alpha=1-exp(-dt/Settings.SmoothingTime)`；关闭平滑则使用原始值。RawTangentSpeed 留作只读对照。首次采样、dt 太小/过大、暂停恢复、位置跳变大于 Settings.TeleportDistance、配置/地面组件切换时重新播种历史，不把缺测显示为 0。距离/时间上限可配，测试高速角色时避免正常步幅触发重置。

仅当 Contact、当前地面/高度/垂直速度有效、连续样本有效且地面被接受为静态时，平滑切向速度超过 Settings.SlipSpeed 连续累计 Settings.SlipDuration，输出 SuspectedSlip。下降到 Settings.SlipExitSpeed 以下持续 Settings.SlipClearTime 后清除；失去当前有效接触或数据重置立即清除并标记 N/A。接触确认和滑步确认分别计时，输出属于启发式报警。

移动平台不在本版的可信滑步范围。初始策略只接受 Static mobility 的命中组件，Movable/Stationary 显示 N/A 并说明原因；GetMobility 已核对并编译；实际跟踪命中组件变换，发生移动则在该次接触期间持续禁用滑步；物理模拟组件也不接受。真实 Movable 碰撞测试已验证 N/A。未来再实现接触点处平台线/角速度补偿，不使用 Owner 速度代替。

## 7. 参数、绘制与生命周期

| 参数组 | 实际属性 | 约束 |
| --- | --- | --- |
| 启用 | bEnableFootDiagnostics、bDrawDebugVisualization、bShowScreenData | 检测和显示独立；关闭检测必须重置并恢复 Mesh 设置 |
| 双脚 | LeftFootBone、RightFootBone、LeftSoleOffset、RightSoleOffset、bAutoFindFootBones | 默认 foot_l/foot_r；偏移需实际标定 |
| 射线 | Settings.TraceStartOffset、TraceLength、MaxGroundSlope；GroundTraceChannel | 长度正值，坡度限制在合理范围；不命中不冒充接触 |
| 接触 | Settings.ContactHeight、ContactExitHeight、ContactVerticalSpeed、ContactExitVerticalSpeed、MaxPenetration、进入/退出确认秒数 | 迟滞有序，时间非负 |
| 速度 | Settings.SmoothingTime、Min/MaxDeltaTime、TeleportDistance | 防除零、掉帧和瞬移尖峰 |
| 滑步 | Settings.SlipSpeed、SlipExitSpeed、SlipDuration、SlipClearTime | Enter > Exit >= 0；时间非负 |

组件参数通过 EditAnywhere 属性配置，Settings 展开为内部 Trace/Contact/Slip/Sampling 类别，LeftFoot/RightFoot 结果只读；运行中变更影响历史的参数后重置。默认值见 BUILD_AND_TEST；Quinn 待机已验证，完整运动标定和误报率仍待确认，不宣称阈值通用。

绘制脚骨点/脚底点、射线、命中点和法线、速度箭头。Contact 绿、Swing 蓝、SuspectedSlip 红、Unknown 黄，不支持地面通过 N/A 原因文字标记，同时写文字避免仅靠颜色。屏幕每脚固定且按组件实例区分的 uint64 key，以 10 Hz 更新同一行；几何和检测逐帧执行，显示 L/R、骨名、相位、命中、h、速度、报警与原因。避免每帧 INDEX_NONE 新行；Key 类型和删除重载已核对并编译验证。短寿命/非持久线，关闭只清理自己的屏幕 key，不清空其他系统调试信息。

EndPlay/Mesh 切换解绑姿态委托和 Tick 依赖、恢复临时设置、清理自己的输出。三个控制台变量在模块中通过 FAutoConsoleVariableRef 注册一次，不在每次 PIE 重复注册。支持 Development Editor 的 PIE 和无窗口 Game 验证；Shipping 构建不是本次验收目标。

## 8. API 证据与编码验证清单

以下路径相对于已确认引擎的 `Engine/Source/Runtime/Engine`，2026-10-13 已只读核对：

| API | 已核对源码 | 已确认范围 |
| --- | --- | --- |
| GetBoneIndex、GetBoneTransform(int32)、GetBoneLocation | `Classes/Components/SkinnedMeshComponent.h`；`Private/Components/SkinnedMeshComponent.cpp` | 声明存在；int32 变换通过组件变换转成世界空间；未注册/索引无效可能返回 Identity，必须提前验证 |
| GetBoneTransform(FName)、GetSocketTransform | 同上 | FName 重载走 Socket 查询，缺失骨可能回退组件变换，不能单凭返回值确认骨存在 |
| GetSkeletalMeshAsset、GetAnimInstance | `Classes/Components/SkeletalMeshComponent.h` | 实际 Quinn / ABP_Unarmed 已运行验证 |
| LineTraceSingleByChannel | `Classes/Engine/World.h` | Owner 忽略、真实碰撞和无命中已验证；坡边画面待验证 |
| TickComponent、AddTickPrerequisiteComponent | `Classes/Components/ActorComponent.h` | 与骨骼 Finalize 委托组合的默认地图连续采样已验证；逐帧画面对齐待验证 |
| VisibilityBasedAnimTickOption、bEnableUpdateRateOptimizations | `Classes/Components/SkinnedMeshComponent.h` | 启用、关闭、EndPlay 恢复已验证；实际开销未测量 |
| DrawDebugLine/Sphere/DirectionalArrow | `Public/DrawDebugHelpers.h` | 声明存在；实际显示、生命周期和构建宏待验证 |
| AddOnScreenDebugMessage、RemoveOnScreenDebugMessage | `Classes/Engine/Engine.h` | uint64/int32 添加重载、uint64 删除声明存在；多实例 key 与显示开关待验证 |

已进一步确认：骨骼 Finalize 委托注册/注销、IsRunningParallelEvaluation、GetNumComponentSpaceTransforms、GetMobility、GetBoneNames、bPauseAnims、控制台 FAutoConsoleVariableRef、UHT 属性与 include 均已编译/运行验证。骨名回退仅在 Mesh/配置变化时扫描实际骨表，优先 foot_l/foot_r、LeftFoot/RightFoot 等明确左右候选，排除 IK/虚拟/扭转控制；原配置名不被改写。暂停恢复另外使用单调时钟检测过长真实采样间隔。尚待验证：PIE 绘制对齐/颜色/屏幕更新，完整步态阈值标定、坡边/台阶、实际 30/60/120 FPS 运行、长时开销与多次 PIE。详见 BUILD_AND_TEST。

已知工程问题与构建状态统一维护在 [BUILD_AND_TEST.md](BUILD_AND_TEST.md)，实施顺序见 [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)。
