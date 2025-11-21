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

本日志总计约 2200 余字，详细记录了 mycraft 从架构、渲染、世界生成到交互调试的主要决策与思路，便于今后继续演进或交接。
