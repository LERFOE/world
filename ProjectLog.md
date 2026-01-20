# 重要！！！！我最后的报告需要分下面几块内容来讲：
1.全局架构（渲染管线，项目总览，主要做了哪些部分的实现）
2.地形生成算法
3.光照实现
4.纹理，云层等的实现
5.处理输入，摄像机，与方块交互

# mycraft Project Log

## 选型与全局架构
本次 Demo 的目标是把 mycraft 落地为一个具备可玩性的体素世界，核心约束是 C++17 + OpenGL 4.1，并在 macOS 上与 GLFW + ImGui 后端配合良好。首先从资源组织切入：GLFW/ImGui/GLM/GLAD/STB 一律通过 CMake FetchContent 下载，以避免在仓库中携带第三方源码，同时确保跨平台一致的依赖版本。渲染层采用一个单一的 `block.vert/block.frag` 管线，所有世界数据（方块、云层、调试线框）通过 `RenderVertex` 结构统一描述，使调度更简单。逻辑层被拆分为 Camera、TextureAtlas、BlockRegistry、Chunk、World 五个模块。Camera 与输入处理保持轻量，以便日后更换控制方式；World 持有 Chunk Map、生成器、可视化辅助等，成为核心调度器。为了让用户的材质包直接可用，TextureAtlas 支持从 `Faithful 64x` 中挑选若干纹理拼装成 GPU 图集，BlockRegistry 再把方块到贴图的映射固定下来。

## 图形管线与 Shader 设计
整个场景渲染只依赖一个着色器。`block.vert` 负责把世界坐标直接送入 VP 矩阵，额外输出法线、UV、顶点色、材质分类、光照权重等插值数据。`block.frag` 则内置了多个分支：默认分支将图集采样颜色与块本身的自定义 tint 混合，再叠加环境光 + 太阳漫反射；`material==1` 的水面注入时间相关的波动并降低 alpha；`material==2` 则走程序化云层支路，通过 GLSL 内部的 FBM 噪声生成滚动云量；`material==3` 用于调试线框，不做贴图采样。Shader 里还处理了 Ray Picking 的高亮动画：将击中方块的坐标作为 uniform，所有像素根据 `floor(fragPos)` 判断是否属于目标方块，从而实现 break 进度条的渐变高亮。雾化采用指数模型，并根据光照调整雾颜色，以保证昼夜过渡时的气氛连贯。ImGui 使用 `#version 410 core`，避免 macOS 下 OpenGL3 backend 与上下文版本不匹配的问题。

## 体素世界与 Chunk Mesh
世界被切成 16×16×128 的 Chunk。`Chunk` 内部存储一个 `std::vector<BlockId>` 表示所有体素，MeshBuilder 在 `buildMesh` 中执行可见面剔除：对每个体素遍历六个面，如果相邻体素不可见，则将该面写入渲染缓冲。面顶点的生成由预定义的 `faceVertices` 表驱动，UV 根据图集中对应 tile 的 uvRect 计算，法线、光照权重、Tint 也在这个阶段写入。透明材质（玻璃、水、广告牌花朵）会进入 alpha VBO，之后以独立 pass 渲染。花朵通过 `billboard` 标记触发交叉平面生成功能，使其看起来不像立方体。Chunk Mesh 上传时使用统一的 VAO 布局，并缓存 solid/alpha 两组 draw call，减少每帧状态切换。

## 世界生成与交互
World 负责三个主要流程：1）根据玩家所在 Chunk，确保以一定半径生成 / 保持 Chunk；2）队列化地重建 Mesh，限制单帧重建数量，避免卡顿；3）进行 Ray Picking、方块修改、UI 数据收集。地形生成通过多层 Perlin FBM + Ridge 噪声叠加，形成低区浅滩、高区雪山的过渡。树木使用噪声触发，在 Chunk 内留出边界避免跨 Chunk 写入，同时构建原木 + 叶子。花朵、沙滩、积雪基于噪声阈值和高度切换。Ray Picking 采用 DDA，支持击中方块法线，方便玩家放置新方块。交互层实现了：WASD+Space/Shift 飞行、鼠标视角、Tab 切换鼠标锁定、数字键切换 Hotbar、左键长按破坏（带渐进动画）、右键放置。ImGui 窗口显示 FPS、相机位置、Chunk 数量、渲染开关（线框/Chunk 边界/云），以及热键选择列表。

## 纹理图集与 Faithful 资源
`TextureAtlas` 读取预设纹理清单（草方块顶/侧、泥土、石头、沙子、砂砾、雪、静水、橡木材、橡木原木、树叶、玻璃、虞美人等），强制每个 tile 尺寸一致（64×64），并按照接近平方的布局拼入大纹理。为了避免采样出血，UV 在 tile 边缘缩进 0.5 像素。Atlas 构建完成后将名称映射存入哈希表，BlockRegistry 就可以直接通过字符串拿到索引，生成每个面的贴图列表。材质包位于 `Faithful 64x - September 2025 Release/assets/minecraft/textures/block`，运行时会从当前工作目录向上搜索直到发现该文件夹，免去手动配置路径。

## 云层、调试工具与 UI
云层由 `World::CloudLayer` 生成一块 2km² 的平面网格，材质设为 2，渲染时使用与方块相同的 Shader，但只走 FBM 支路，配合 `uCloudOffset`、`uCloudTime` 实现平滑滚动。Chunk 边界调试通过 `boundsVertices_` 在 CPU 端生成若干线段，并以 `material==3` 的方式渲染，ImGui 勾选后即可查看。UI 方面除了核心信息外，特地调试了 ImGui 在 macOS Core Profile 上 shader 版本不匹配导致的崩溃，将后端初始化参数固定在 `#version 410`，并在窗口初始化时启用了 `GLFW_OPENGL_FORWARD_COMPAT`。此外在 Shader 中也引入了 `uCloudEnabled` 等 uniform，确保在 UI 关闭云层时不会产生残影。

## 输入、摄像机与交互感受
Camera 默认 FOV 60°，Z 远裁剪设为 1000，足以覆盖 render distance 6 的体素范围。鼠标灵敏度固定为 0.08，Tab 用于切换鼠标捕获。移动速度默认 7.5m/s，可通过 Ctrl 加速。由于本 Demo 没有碰撞体，玩家可在空中自由飞行，用于调试地形。Ray Picking 距离设置为 8.5 方块，破坏时间约 0.35s。为了提升破坏反馈，在 Shader 中实现了与进度同步的暖色调高亮，并通过 ImGui 的“选中方块”列表展示当前 Hotbar 选择。

## 构建与依赖
CMakeLists 重新整理为 `project(mycraft LANGUAGES C CXX)`，集成 GLAD、GLFW、GLM、ImGui 与 stb。GLAD 源码通过 `glad-c` 分支提供的纯 C loader，避免额外的 Python 生成步骤。stb 直接抓取官方仓库，再由 TextureAtlas 单元包含 `stb_image.h`。为了防止 macOS 系统头 `Block.h` 冲突，方块相关的头文件命名为 `voxel_block.h`。编译时默认开启 `-Wall -Wextra -Wshadow -Wconversion -Wpedantic`，并对 Mesh 上传等处添加 `static_cast<GLsizeiptr>`，确保在开启严格告警后仍能成功构建。测试构建在 `build-mycraft` 目录完成，可直接 `cmake --build build-mycraft`。

## 测试与验证
功能验证主要覆盖：
1. CMake 配置 + 构建：在 macOS 14 + AppleClang 上全量编译，确认依赖下载、ImGui 后端与 OpenGL 4.1 兼容。
2. Shader 参数：模拟日夜循环与鼠标破坏，确认 Shader 中的 uniform 变化正确；云层在关闭/开启时无残影。
3. Ray Picking：使用多种方块堆叠测试 DDA 结果，确保 `hit.normal` 指向放置方向并且距离 8.5 方块时终止。
4. Chunk 管理：移动到 render distance 边缘，确认旧 Chunk 从 unordered_map 中回收，新的 Chunk 逐步生成且 Mesh Build 队列不会拖垮帧率。
5. UI/交互：Tab 捕获、数字键切换 Hotbar、ImGui 勾选开关、线框模式。特别检查了 macOS Retina 下的 framebuffer size，保持 Viewport/Projection 同步。

## 未来迭代方向
- **性能**：Chunk Mesh 仍为逐方块面片构建，可进一步实现 Greedy Meshing、Mesh Caching，或异步线程生成。
- **世界玩法**：目前只有放置/破坏，未来可以添加简易库存、方块掉落、chunk 保存、流水/重力等机制来提升可玩性。
- **渲染效果**：可加入阴影、使用 IBL/天空盒、屏幕空间雾、PBR 等技术让 Faithful 材质更具表现力。
- **音效与 UI**：接入 OpenAL / SDL Mixer 播放挖掘、脚步等提示，UI 可扩展为调试面板 + 迷你地图。

## 2026-01-18 更新日志：地形与植被多样性升级

### 1. 复杂地形与生物群系系统
引入了基于 **温度 (Temperature)**、**湿度 (Humidity)** 和 **大陆性 (Continentalness/HeightStr)** 的多维度噪声模型，取代了单一的高度图生成。
- **新增 BiomeType**：实现了 Ocean, Beach, Plains, Forest, Desert, Mountains, SnowyTundra, Swamp 八大群系。
- **河流系统**：利用负相噪声（River Noise）在地形中侵蚀出蜿蜒的河道。
- **分布逻辑**：
  - 高温干燥 -> 沙漠 (Desert)
  - 极寒 -> 冻原 (SnowyTundra)
  - 湿润 -> 森林 (Forest) 或 沼泽 (Swamp)
  - 沿海过渡带 -> 沙滩 (Beach)

### 2. 丰富植被与方块扩展
大幅扩展了植物库，并适配 Faithful 64x 资源包材质。
- **新方块注册**：
  - 花卉：Poppy, Dandelion, BlueOrchid, Allium, AzureBluet, Tulips (Red/Orange/White/Pink), OxeyeDaisy, Cornflower, LilyOfTheValley。
  - 沙漠植物：Cactus (仙人掌), DeadBush (枯灌木)。
  - 其他：TallGrass (高草)。
- **混种分布算法**：
  - 摒弃了单一的成片生成，采用高频噪声进行“混织”生成，使花田呈现自然的杂色分布。
  - **群系特异性**：
    - 沙漠：仅生成仙人掌和枯灌木，无草皮。
    - 森林：高密度树木，伴生少量花草。
    - 平原：低密度树木，大片混合花海（郁金香、雏菊等）。
    - 沼泽：生成特定的兰花 (BlueOrchid)。

### 3. 可视化与材质
- **Texture Mapping**：更新 `buildTextureList`，建立了完整的 BlockId 到 Faithful 材质 PNG 的映射表。
- **物理与交互**：更新了 AABB 碰撞检测逻辑，增加扫掠检测（Swept AABB），解决了快速移动时的穿模问题。
- **氛围渲染**：前期已优化了 Tone Mapping 与迷雾效果，适配不同生物群系的视觉感受。

### 已知问题与优化项
- **沙滩范围**：当前参数下沙滩区域可能过于宽阔，后续可调整 `heightScale` 阈值（如 [0.0, -0.05]）收窄过渡带。
- **树木穿插**：在区块边界处的树木生成逻辑已加强检查，但在极高密度的森林中仍可能有少量叶片修剪瑕疵。

### 3. 光照实现
当前光照系统采用 **"CPU 预计算 AO + 静态平行光 + 着色器动态合成"** 的混合方案，兼顾了性能与体素世界的经典视觉风格。

#### 3.1. 顶点光照 (Baked Lighting)
光照信息在 Chunk Mesh 构建阶段被直接写入顶点数据 (`RenderVertex.light`)，避免了复杂的实时阴影计算。
- **环境光遮蔽 (Ambient Occlusion, AO)**：
  - 算法：对每个体素顶点的邻域（side1, side2, corner）进行采样检测。如果相邻方块也是实体，则会遮挡环境光。
  - 逻辑：`occlusion` 等级（0~3）决定光照衰减系数。
    - 0 遮挡 → 100% 亮度
    - 1 遮挡 → 80% 亮度
    - 2 边遮挡 → 60% 亮度
    - 3 全角遮挡 → 40% 亮度
  - 实现：见 `src/chunk.cpp` 中的 `vertexAO` 函数。
- **面光照 (Face Lighting)**：
  - 根据方块面的法线方向，应用预设的亮度乘数，增强立体感：
    - Top (+Y): 1.2 (最亮)
    - Front/Back (+/-Z): 1.0
    - Right/Left (+/-X): 0.92
    - Bottom (-Y): 0.7
  - 这种简单的技巧使得即使在无纹理或纯色情况下，方块也能呈现出明确的体积感。

#### 3.2. 全局动态光照 (Shader Synthesis)
在 `shaders/block.frag` 中并将预计算的 AO 与实时环境参数合成。
- **光照模型**：
  - `Ambient = uAmbient * Albedo * AO`
  - `Diffuse = uSunColor * Albedo * NdotL * AO` (其中 NdotL 为法线与太阳方向点积)
  - `Total = Ambient + Diffuse`
- **日夜循环**：
  - `world.cpp` 中的 `updateSun` 函数驱动太阳方向 (`uSunDir`) 和颜色 (`uSunColor`)。
  - **环境光 (uAmbient)**：从深夜的深蓝 (0.02, 0.04, 0.08) 平滑过渡到白天的天空蓝 (0.35, 0.43, 0.54)。
  - **太阳光 (uSunColor)**：黎明/黄昏呈暖橙色，正午呈亮白色。直射光强度在日落后迅速衰减至 0。
- **雾化系统**：
  - 使用指数雾 (`exp(-density * dist)`) 混合天空颜色与场景，`uFogDensity` 随日夜变化（夜晚与日出时雾更浓），掩盖 Render Distance 边缘的截断。

#### 3.3. 特殊材质光照
- **自发光**：BlockInfo 中的 `emission` 属性直接叠加在顶点光照值上，使岩浆或光源方块在暗处依然可见（代码中已预留 `info.emission` 字段）。
- **水面反射**：水面材质通过菲涅尔项 (Fresnel) 模拟太阳的高光反射，增强水体的质感。

## 2026-01-19 更新日志：放置方块与滚轮切换

### 1. 放置草方块/沙子/花朵的交互链路
本次功能基于已有的 Ray Picking 与放置接口补全“可玩”的放置体验，核心路径为：
- **Raycast 选中**：每帧通过 `world.raycast(camera.position(), camera.forward(), 8.5f)` 得到击中的方块坐标 `hit.block` 与法线 `hit.normal`。
- **右键放置**：在按下鼠标右键时，将 `hit.block + hit.normal` 作为目标格子，调用 `world.placeBlock(place, hotbar[selectedSlot])` 写入世界方块并触发 Mesh 重建。
- **可放置列表**：Hotbar 改为 `{Grass, Sand, Poppy, Dandelion, BlueOrchid, RedTulip, OxeyeDaisy, Cornflower}`，保证草地、沙地与多种花可直接放置。

### 2. 鼠标滚轮切换方块（支持平滑滚轮）
为兼容鼠标滚轮/触控板的细粒度增量，采用累计 delta + 步进转换的方式：
- **滚轮回调接入**：注册 `glfwSetScrollCallback`，把 `yoffset` 累积到 `gScrollDelta`，并链式调用旧回调以保持 ImGui 的滚动行为。
- **步进换算**：当 `gScrollDelta >= 1` 时取 `floor`，当 `<= -1` 时取 `ceil`，将结果映射为 `steps`。
- **循环选择**：`selectedSlot = (selectedSlot - steps) % hotbar.size()`，同时对负数取模做修正，实现滚轮上下滚的循环切换。
- **输入抑制**：当鼠标未锁定或 ImGui 需要鼠标时，直接清空滚轮 delta，避免 UI 滚动时误切换。

### 3. UI 显示补全
- `blockName` 扩展了花卉/仙人掌等新方块名称，保证 ImGui 的“方块选择”列表与实际 Hotbar 一致。

本日志持续更新，用于记录 mycraft 的关键实现细节与设计决策，便于后续完善与复盘。

## 2026-01-20 更新日志：准星与交互距离调整

### 1. 屏幕中心准星（灰色半透明十字）
为方便定位放置目标，在 UI 绘制阶段使用 ImGui 的前景绘制层添加了中心十字：
- **绘制位置**：基于 `io.DisplaySize` 计算屏幕中心点。
- **外观参数**：十字半长 8px、线宽 2px、颜色为灰色半透明（`RGBA(160,160,160,160)`）。
- **渲染层级**：通过 `ImGui::GetForegroundDrawList()` 确保准星位于 HUD 之上且不被场景遮挡。

### 2. 方块交互距离调整为 7
将方块射线检测距离统一改为 `7.0f`，影响交互链路如下：
- **Raycast 距离**：`world.raycast(camera.position(), camera.forward(), 7.0f)`。
- **联动效果**：方块高亮、破坏进度、右键放置均使用同一条射线，因此最大交互距离一并更新为 7。
