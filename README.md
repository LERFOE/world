# 计算机图形学项目报告：Mycraft 体素世界游戏

## 项目概述

本项目实现了一个基于 C++17 和 OpenGL 4.1 的体素放置游戏。整体设计参考了 Minecraft 的核心机制，涵盖地形生成、光照系统、纹理映射、交互逻辑等关键模块。项目在 macOS 平台上完成开发与测试，使用 CMake 管理第三方依赖（GLFW、GLM、GLAD、ImGui、stb_image），材质使用Faithful 64x 材质包。

本项目系统性地实践了现代图形学渲染管线的核心概念，包括顶点处理、着色器编程、纹理映射、光照模型等。同时，项目深入探索了体素引擎特有的技术挑战，如大规模场景的内存管理、网格优化以及程序化内容生成。

---

## 1. 全局架构

### 1.1 项目结构与模块划分

本项目在架构设计上采用了**模块化分层架构**的思路。该设计的核心考量在于：将渲染、逻辑、数据三层解耦，使各模块可独立开发和测试。例如，`Chunk` 模块仅负责方块数据的存储和网格生成，与渲染细节完全分离；`World` 模块则作为协调者，管理 Chunk 的生命周期并处理用户交互。

核心模块包括：

```
src/
├── main.cpp              // 主循环与 OpenGL 上下文初始化
├── camera.h/cpp          // 摄像机与视图矩阵管理
├── shader.h/cpp          // 着色器程序封装
├── texture_atlas.h/cpp   // 纹理图集构建与 UV 映射
├── voxel_block.h         // 方块 ID 与 BlockInfo 定义
├── block.cpp             // 方块注册表（BlockRegistry）
├── chunk.h/cpp           // Chunk 数据结构与 Mesh 构建
├── world.h/cpp           // 世界管理、地形生成、交互逻辑
├── raycast.h/cpp         // DDA 光线投射算法
└── mesh.h                // 渲染顶点结构定义

shaders/
├── block.vert            // 统一顶点着色器
├── block.frag            // 统一片元着色器（含多材质分支）
└── shadow.vert/frag      // 阴影贴图预留
```

在模块划分上，本项目遵循以下**核心设计原则**：

**统一着色器管线**：所有渲染对象（方块、水、云层、调试线框）共用一套 Shader，通过 `material` 字段进行分支处理。采用该设计的目的是减少 OpenGL 状态切换的开销——在体素世界中，每一帧可能需要渲染数万个面片，频繁切换 Shader 将带来显著的性能损失。

**ECS 思想的体素结构**：本项目借鉴 Entity-Component-System 的思想组织数据。`BlockInfo` 作为组件，定义方块的各种属性（透明度、材质、发光、生物群系染色等）；`Chunk` 作为容器，负责存储方块数据并生成渲染网格；`World` 作为系统，管理所有 Chunk 的生命周期。这种分离使代码更易于维护和扩展。

**延迟网格重建**：当玩家破坏或放置方块时，系统并不立即重建整个 Chunk 的网格，而是将其标记为"脏"并加入重建队列。每一帧仅处理有限数量的重建任务，从而避免单帧卡顿。这是在实时交互系统中尤为重要的一种性能优化策略。

### 1.2 系统功能模块

除了核心渲染引擎，本项目还扩展了完整的游戏功能支持：

**存档与加载系统**：
*   **持久化存储**：支持将世界状态（种子、玩家坐标、时间戳）序列化为 `.dat` 文件。
*   **启动菜单**：实现了基于 ImGui 的主菜单，支持创建新世界（随机/指定种子）和加载历史存档。
*   **动态世界管理**：游戏逻辑支持在运行时重置 `World` 实例，实现从菜单进入游戏的无缝切换。

### 1.3 渲染管线流程

整个渲染流程分为四个层次，按照数据流动的顺序设计如下：

```
[输入层] GLFW 事件监听 → Camera 状态更新
    ↓
[逻辑层] World::update
    ├─ 根据相机位置加载/卸载 Chunk
    ├─ 队列化重建 Dirty Chunk 的 Mesh
    └─ 更新日夜循环与天气状态
    ↓
[渲染层] World::render
    ├─ 设置 Uniform (VP 矩阵、太阳方向、雾参数等)
    ├─ 渲染不透明方块
    ├─ 渲染透明方块（关闭深度写入）
    ├─ 渲染云层（FBM 程序化噪声）
    └─ 渲染调试线框
    ↓
[UI层] ImGui 覆盖层
    └─ FPS、相机位置、Chunk 统计、控制面板
```

**输入层**负责捕获用户的键盘和鼠标事件，并实时更新摄像机状态。GLFW 提供了跨平台的窗口管理能力，使开发者可专注于渲染逻辑本身。

**逻辑层**是每一帧的核心处理阶段。`World::update` 函数根据摄像机当前位置，动态加载视野范围内的 Chunk，同时卸载超出范围的 Chunk 以释放内存。此处采用了一项关键的设计决策：将 Chunk 的加载与网格重建分离处理。加载时仅生成地形数据，网格重建则放入队列延迟执行，每帧最多重建 4 个 Chunk。

**渲染层**执行实际的 OpenGL 绘制调用。本项目将不透明物体和透明物体分为两个渲染阶段（Pass）。不透明物体先渲染，写入深度缓冲；透明物体后渲染，关闭深度写入但保留深度测试。该策略可正确处理透明物体的遮挡关系。

**UI 层**使用 ImGui 实现调试面板，实时显示帧率、摄像机位置、已加载 Chunk 数量等信息，有助于开发调试。

### 1.3 核心技术实现

#### 1.3.1 RenderVertex 结构

在设计顶点格式时，需要在灵活性和性能之间做出权衡。本项目定义了一个统一的 `RenderVertex` 结构来描述所有几何体：

```cpp
struct RenderVertex {
    glm::vec3 pos;      // 世界空间位置
    glm::vec3 normal;   // 法线（用于光照计算）
    glm::vec2 uv;       // 纹理坐标（图集 UV）
    glm::vec3 color;    // 顶点颜色（Tint + Emission）
    float light;        // 预计算光照值（AO 与面光照）
    float material;     // 材质分类标识
    glm::vec3 anim;     // 动画参数
};
```

该结构的设计经过多次迭代。最初仅包含 `pos`、`normal`、`uv` 三个基本属性，随着功能扩展，逐步添加了更多字段：

- `color` 字段用于存储生物群系染色信息，使草地和树叶可根据所在区域显示不同的颜色。
- `light` 字段存储预计算的光照值，包括环境光遮蔽（AO）和面光照。将光照"烘焙"到顶点中，可避免片元着色器中的复杂计算。
- `material` 字段是一个浮点数标识，用于在片元着色器中区分不同类型的材质，从而执行不同的渲染逻辑。
- `anim` 字段用于水面等需要动画的材质，存储帧数和速度信息。

虽然该结构的内存占用较大（每个顶点约 64 字节），但其带来的灵活性是值得的——同一套渲染代码可处理各种不同的几何体。

#### 1.3.2 Shader 分支逻辑

在 `block.frag` 中，通过 `material` 字段实现了多路复用。该设计的优点在于仅需维护一套着色器代码，同时又能针对不同材质执行特定的渲染逻辑：

| Material ID | 用途           | 特殊处理                          |
|-------------|----------------|-----------------------------------|
| 0.0 (默认)  | 普通方块       | 纹理采样 + 环境光/漫反射混合      |
| 1.0 ± 0.1   | 水面           | 时间波动 UV + 菲涅尔高光 + 半透明 |
| 1.15 ± 0.05 | 玻璃           | 强高光 + 半透明                   |
| 2.0         | 云层           | 程序化 FBM 噪声                   |
| 4.0 ± 0.5   | 动物           | 独立纹理采样 (Pig/Cow/Sheep)      |
| 5.0 ± 0.5   | 太阳           | Billboard 自发光，无光照计算      |

这种基于 `material` 字段的分支设计，使系统可在一次 Draw Call 中渲染多种不同的材质。GPU 对分支语句的处理效率取决于 Warp/Wavefront 内的一致性——若同一批次的像素走同一个分支，性能损失很小。在体素世界中，相邻像素通常属于同一种材质，因此该设计是可行的。

#### 1.3.3 Chunk 管理策略

Chunk 是体素世界中最核心的数据结构。每个 Chunk 包含 16×128×16 = 32768 个方块，需要高效地管理其加载、渲染和卸载。

**动态加载机制**：以相机为中心，系统维护一个半径为 8 个 Chunk 的"活跃区域"。当相机移动时，新进入范围的 Chunk 被生成并加载，超出范围的 Chunk 则被卸载。这种流式加载策略使玩家可在无限大的世界中自由探索，同时内存占用保持在可控范围内。

**Mesh 缓存策略**：每个 Chunk 维护两个 VAO，分别存储不透明方块和透明方块的网格数据。网格生成是一个 CPU 密集型操作——需遍历所有方块，检测哪些面需要渲染，计算 UV 坐标和光照值。因此，系统仅在方块被修改时标记 Chunk 为"脏"并重建网格，以避免重复计算。

**内存回收**：Chunk 数据存储在一个以坐标为键的 `unordered_map` 中。当 Chunk 超出渲染距离时，系统将其从 map 中移除，析构函数自动释放 CPU 内存和 GPU 资源（VAO、VBO）。这种 RAII 风格的资源管理可有效避免内存泄漏问题。

---

## 2. 地形生成算法

### 2.1 算法设计动机与总体架构

#### 2.1.1 设计目标

程序化地形生成是体素游戏的核心技术之一。本项目的地形生成算法需满足以下设计目标：

1. **自然性**：生成的地形应具有真实世界的特征，包括连续的山脉、蜿蜒的河流、平滑的高度过渡。
2. **多样性**：不同的随机种子应产生完全不同的地形，避免重复感。
3. **性能**：算法需在实时运行，每帧能生成多个 Chunk（16×128×16 方块）的地形数据。
4. **可控性**：通过调整参数可灵活控制地形特征（如山脉高度、河流密度、生物群系分布）。

#### 2.1.2 技术路线选择

在技术实现上，本项目选择了 **基于噪声函数的程序化生成** 方案。相较于其他方案（如高度图导入、预制地形块拼接），噪声函数具有以下优势：

| 方案                | 优点                           | 缺点                           | 本项目选择 |
|---------------------|--------------------------------|--------------------------------|------------|
| 高度图导入          | 地形完全可控，易于艺术设计     | 内存占用大，无法生成无限世界   | ✗          |
| 预制地形块拼接      | 实现简单，性能好               | 接缝明显，多样性差             | ✗          |
| **噪声函数生成**    | **无限世界，多样性高，性能优** | **参数调优难度大**             | **✓**      |

#### 2.1.3 算法架构概览

本项目的地形生成流程分为 **五个独立但相互关联的阶段**，每个阶段使用不同频率和类型的噪声函数：

```
[阶段1] 3D 坐标空间映射
   ↓ 将世界坐标 (x, z) 和随机种子 seed 转换为 3D 噪声坐标 (u, v, w)
[阶段2] 气候场生成
   ↓ 使用低频 FBM 噪声计算 Temperature 和 Humidity
[阶段3] 地形骨架构建
   ↓ 使用极低频噪声计算 Continental（大陆性），决定海洋/陆地/山脉的基础分布
[阶段4] 地形细节雕刻
   ↓ 叠加中高频噪声添加丘陵、河流等地形细节
[阶段5] 表面装饰
   ↓ 根据高度和生物群系放置方块（草、沙、石头等）
```

每个阶段的输出作为下一阶段的输入，形成清晰的数据流动链条。接下来将逐一详细展开每个阶段的数学原理和代码实现。

---

### 2.2 噪声函数基础理论

#### 2.2.1 Perlin 噪声原理

Perlin 噪声是一种梯度噪声（Gradient Noise），由 Ken Perlin 于 1983 年发明。其核心思想是在规则网格的顶点上定义随机梯度向量，然后通过插值计算任意点的噪声值。

**数学定义**：对于 3D 空间中的点 $\mathbf{p} = (x, y, z)$，Perlin 噪声的计算步骤如下：

1. **网格定位**：计算 $\mathbf{p}$ 所在的单位立方体的 8 个顶点坐标：
   $$\mathbf{v}_{ijk} = (\lfloor x \rfloor + i, \lfloor y \rfloor + j, \lfloor z \rfloor + k), \quad i,j,k \in \{0,1\}$$

2. **梯度向量查询**：为每个顶点 $\mathbf{v}_{ijk}$ 分配一个伪随机梯度向量 $\mathbf{g}_{ijk}$（通过哈希函数生成）。

3. **距离向量计算**：计算 $\mathbf{p}$ 到每个顶点的距离向量：
   $$\mathbf{d}_{ijk} = \mathbf{p} - \mathbf{v}_{ijk}$$

4. **点积计算**：计算每个顶点的影响值（距离向量与梯度向量的点积）：
   $$\text{influence}_{ijk} = \mathbf{d}_{ijk} \cdot \mathbf{g}_{ijk}$$

5. **三线性插值**：使用平滑插值函数（Hermite 插值）对 8 个影响值进行插值，得到最终噪声值：
   $$\text{noise}(\mathbf{p}) = \text{trilinear\_interp}(\text{influence}_{ijk}, \text{fade}(x_f), \text{fade}(y_f), \text{fade}(z_f))$$

   其中 $\text{fade}(t) = 6t^5 - 15t^4 + 10t^3$ 是平滑函数，确保噪声在网格边界处的导数为零。

**关键性质**：
- **连续性**：Perlin 噪声在所有点处连续且可微。
- **值域**：输出范围约为 $[-1, 1]$（实际实现中需归一化到 $[0, 1]$）。
- **频率不变性**：单层 Perlin 噪声只有一个特征频率，地形会显得单调。

![图2-1: Perlin 噪声示意图 - 展示单层 2D Perlin 噪声的灰度图]

#### 2.2.2 分形布朗运动（Fractional Brownian Motion）

为解决单层噪声的单调性问题，本项目采用 **分形布朗运动（FBM）** 技术叠加多个频率的噪声。

**数学定义**：FBM 是将多个不同频率的噪声加权叠加的过程：

$$\text{FBM}(\mathbf{p}) = \sum_{i=0}^{n-1} A_i \cdot \text{noise}(F_i \cdot \mathbf{p} + \mathbf{offset}_i)$$

其中：
- $n$ 是叠加层数（octaves）
- $A_i = A_0 \cdot \text{persistence}^i$ 是第 $i$ 层的振幅
- $F_i = F_0 \cdot \text{lacunarity}^i$ 是第 $i$ 层的频率
- $\mathbf{offset}_i$ 是偏移向量，用于去相关

**参数说明**：

| 参数          | 符号            | 典型值 | 作用                                |
|---------------|-----------------|--------|-------------------------------------|
| Octaves       | $n$             | 2-6    | 控制细节层次：值越大，细节越丰富    |
| Persistence   | $p$             | 0.5    | 振幅衰减率：值越小，高频细节越弱    |
| Lacunarity    | $l$             | 2.0    | 频率增长率：通常为 2，每层频率翻倍  |
| Initial Freq  | $F_0$           | 0.3-1.5| 初始频率：控制地形的整体尺度        |
| Initial Amp   | $A_0$           | 1.0    | 初始振幅：通常归一化为 1            |

**代码实现**（`world.cpp`）：

```cpp
// FBM 函数：在 3D 空间中叠加多层 Perlin 噪声
float fbm(const glm::vec3& p, int octaves, float lacunarity, float persistence) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;  // 用于归一化
    
    for (int i = 0; i < octaves; ++i) {
        // glm::perlin 是 GLM 库提供的 3D Perlin 噪声函数
        sum += amplitude * glm::perlin(p * frequency);
        maxValue += amplitude;
        
        amplitude *= persistence;  // 振幅衰减
        frequency *= lacunarity;   // 频率增长
    }
    
    // 归一化到 [-1, 1] 范围
    return sum / maxValue;
}
```

**效果对比**：

| Octaves | 视觉效果                           | 应用场景               |
|---------|------------------------------------|-----------------------|
| 1       | 平滑波动，无细节                   | 不适用                |
| 2       | 大尺度起伏，缺少小细节             | 气候场（温度/湿度）   |
| 3-4     | 自然地形，中等细节                 | 地形高度图            |
| 5-6     | 高度细节，可能出现噪声感           | 岩石纹理、云层        |

![图2-2: 不同 Octaves 的 FBM 效果对比 - 展示 1/2/4/6 octaves 的地形剖面图]

#### 2.2.3 3D 噪声与随机种子处理

**浮点精度问题**：在早期实现中，本项目尝试通过将大整数种子（如 `seed = 12345678`）直接加到 2D 坐标上来实现随机化：

```cpp
// ❌ 错误做法：会导致精度丢失
float noise = glm::perlin(glm::vec2(worldX + seed, worldZ + seed));
```

该方案存在严重问题：当世界坐标 `worldX` 较大时（如 10000），加上更大的种子后，浮点数的有效精度从 23 位下降到约 10-15 位（因为指数部分需表示更大的数值范围）。这导致地形细节完全丢失，所有方块变成水面（高度值几乎相同）。

**解决方案**：本项目采用 **3D 噪声空间映射** 方案，将种子作为第三维度的坐标：

```cpp
// ✓ 正确做法：利用 3D 噪声空间
glm::vec3 noiseCoord = glm::vec3(
    worldX * 0.002f,        // X 坐标缩放到合理范围
    worldZ * 0.002f,        // Z 坐标缩放到合理范围
    seed_ * 0.1337f         // 种子映射为 Z 轴坐标
);
float noise = glm::perlin(noiseCoord);
```

**数学原理**：3D Perlin 噪声在 $(x, y, z)$ 空间中生成，任意两个不同的 $z$ 值对应完全不同的噪声场。通过将种子映射到 $z$ 轴，不同种子实际上是在 3D 噪声空间中"切片"不同的 2D 平面，从而获得完全独立的地形分布，同时保持坐标精度。

**参数选择**：
- 缩放因子 `0.002f`：将世界坐标（通常 0-10000）映射到噪声空间的合理范围（0-20），避免超出噪声函数的有效频率范围。
- 种子因子 `0.1337f`：将整数种子（0-999999）映射到 $z$ 轴，魔数 `0.1337` 无特殊含义，仅用于打破规律性。

![图2-3: 2D噪声加偏移 vs 3D噪声切片 - 对比两种方案在大坐标下的精度差异]

---

### 2.3 地形生成流程详解

#### 2.3.1 坐标空间映射（Stage 1）

**目标**：将世界坐标系 $(worldX, worldZ)$ 和随机种子 $seed$ 转换为适合噪声采样的 3D 坐标 $(u, v, w)$。

**实现代码**（`world.cpp::generateTerrain`）：

```cpp
// 输入：世界坐标 (worldX, worldZ) 和随机种子 seed_
int worldX = origin.x + x;  // Chunk 内局部坐标 + Chunk 世界偏移
int worldZ = origin.z + z;

// 输出：3D 噪声坐标
glm::vec3 uv = glm::vec3(
    static_cast<float>(worldX) * 0.002f,
    static_cast<float>(worldZ) * 0.002f,
    static_cast<float>(seed_) * 0.1337f
);
```

**数学映射关系**：

$$
\begin{pmatrix} u \\ v \\ w \end{pmatrix} = \begin{pmatrix} 0.002 & 0 & 0 \\ 0 & 0.002 & 0 \\ 0 & 0 & 0.1337 \end{pmatrix} \begin{pmatrix} worldX \\ worldZ \\ seed \end{pmatrix}
$$

**参数说明**：
- **0.002** 是水平缩放因子，决定了地形的"尺度"。该值越小，相同距离内的噪声变化越少，地形越平坦；越大则起伏越频繁。实验表明，0.002 能产生符合直觉的地形尺度（约每 500 米一个大的地形特征）。
- **0.1337** 是种子缩放因子，确保不同种子在 $w$ 轴上有足够的间隔，避免相邻种子产生相似地形。

#### 2.3.2 气候场生成（Stage 2）

**目标**：生成两个独立的标量场 $T(x,z)$ 和 $H(x,z)$，分别表示温度和湿度，用于后续的生物群系判定。

**算法设计**：

```cpp
// 温度场：使用 2-octave FBM，频率 0.5
float tempNoise = fbm(uv * 0.5f, 2, 2.0f, 0.5f);

// 湿度场：同样使用 2-octave FBM，但添加空间偏移以去相关
float humidNoise = fbm(uv * 0.5f + glm::vec3(123.4f, 0.0f, 0.0f), 2, 2.0f, 0.5f);
```

**参数分析**：

| 参数        | 值    | 设计意图                                                   |
|-------------|-------|------------------------------------------------------------|
| 频率 0.5    | 低频  | 形成大范围的气候带（如跨越数千米的沙漠或森林）             |
| Octaves = 2 | 少层  | 气候变化应平滑，不需要太多细节                             |
| 偏移 123.4  | 去相关| 确保温度和湿度场独立，避免"温度高的地方湿度也高"的不自然现象|

**数学表达**：

$$T(\mathbf{p}) = \text{FBM}(0.5 \mathbf{p}, \text{octaves}=2)$$
$$H(\mathbf{p}) = \text{FBM}(0.5 \mathbf{p} + (123.4, 0, 0), \text{octaves}=2)$$

其中 FBM 的输出范围为 $[-1, 1]$。

**效果说明**：
- 温度场 $T$ 值较高的区域（$T > 0.5$）会生成沙漠或热带草原。
- 湿度场 $H$ 值较高的区域（$H > 0.5$）会生成森林或沼泽。
- 两者组合形成 2D 气候分类空间（详见 2.4 节生物群系系统）。

![图2-4: 温度场和湿度场的热力图 - 展示 T 和 H 在一个 512×512 区域的分布]

#### 2.3.3 地形骨架构建（Stage 3）

**目标**：生成大陆性标量场 $C(x,z)$，决定每个坐标点是海洋、平原还是山脉。

**算法设计**：

```cpp
// 大陆性场：使用 3-octave 极低频 FBM
float continental = fbm(uv * 0.3f, 3, 2.0f, 0.5f);
```

**参数选择**：
- **频率 0.3**：极低频，确保地形特征的尺度极大（数千米）。这产生了"大陆"和"海洋"的宏观分布。
- **Octaves = 3**：相比气候场多一层，添加中尺度的起伏（如大型山脉走向）。

**高度映射函数**：

Continental 值被映射为基础高度 $h_{base}$ 和起伏幅度 $A_{terrain}$：

```cpp
float baseHeight = 35.0f + continental * 10.0f;
float amp = 6.0f;

if (continental > 0.3f) {
    // 山地区域：非线性抬升
    float t = (continental - 0.3f);  // t ∈ [0, 0.7]（假设 continental ∈ [-1,1]）
    baseHeight += t * 60.0f;         // 最大增加 42 米
    amp += t * 60.0f;                // 起伏幅度显著增加
} else if (continental < -0.1f) {
    // 海洋区域：非线性下降
    float t = -(continental + 0.1f); // t ∈ [0, 0.9]
    baseHeight -= t * 20.0f;         // 最大下降 18 米
}
```

**分段映射函数可视化**：

| Continental 值 | 地形类型 | baseHeight 范围 | amp 范围 | 视觉特征          |
|----------------|----------|-----------------|----------|-------------------|
| $< -0.5$       | 深海     | 20-25           | 6        | 平坦海底          |
| $[-0.1, 0.3]$  | 平原/丘陵| 33-38           | 6-8      | 起伏平缓          |
| $[0.3, 0.7]$   | 山地     | 38-70           | 8-30     | 显著起伏，峰峦叠嶂|
| $> 0.7$        | 高山     | 70-90           | 30-50    | 极端高度，险峻山峰|

**数学建模思路**：

该分段函数模拟了板块构造理论：
1. **海洋区域**（$C < -0.1$）：对应洋壳，密度大，海拔低。
2. **过渡区域**（$-0.1 < C < 0.3$）：对应大陆架和平原，高度缓慢上升。
3. **山地区域**（$C > 0.3$）：对应造山带，由于非线性项 $t \cdot 60$，高度加速上升，形成陡峭山脉。

![图2-5: Continental 值与地形高度的映射关系图 - 绘制 baseHeight 和 amp 随 continental 变化的曲线]

#### 2.3.4 地形细节雕刻（Stage 4）

在有了宏观骨架后，需要添加中小尺度的地形细节。

**（1）细节噪声叠加**

```cpp
float detail = fbm(uv * 2.0f, 4, 2.0f, 0.5f);
int height = static_cast<int>(baseHeight + detail * amp);
```

**参数说明**：
- **频率 2.0**：较高频率，产生局部起伏（丘陵、小山包）。
- **Octaves = 4**：添加丰富的细节层次。
- **振幅 $A_{terrain}$**：由 Stage 3 计算得到，确保山地的细节比平原更丰富（因为 $amp$ 在山地更大）。

**数学表达**：

$$h_{final}(\mathbf{p}) = h_{base}(\mathbf{p}) + A_{terrain}(\mathbf{p}) \cdot \text{FBM}(2.0\mathbf{p}, 4)$$

其中 $h_{base}$ 和 $A_{terrain}$ 都是 $C(\mathbf{p})$ 的函数（见 Stage 3）。

**（2）河流系统生成**

河流是通过"负脉络"（Negative Vein）技术实现的。其核心思想是：在噪声值接近零的区域"挖掉"地形，形成河道。

```cpp
// 河流噪声：使用 abs() 将噪声值映射为距离场
float riverNoise = std::abs(fbm(uv * 1.5f, 4, 2.0f, 0.5f));

// 平滑阈值函数：将距离场转换为河流遮罩
riverNoise = 1.0f - glm::smoothstep(0.02f, 0.1f, riverNoise);

// 河流侵蚀：在 riverNoise > 0 的区域降低地形高度
if (riverNoise > 0.0f) {
    int erosion = static_cast<int>(10.0f * riverNoise);
    height -= erosion;
}
```

**算法原理**：

1. **绝对值操作**：$|noise|$ 将噪声值映射为"到零值线的距离"。接近零值线的区域（噪声原始值约 $\pm 0.02$）形成连续的"山谷"。

2. **Smoothstep 函数**：
   $$\text{smoothstep}(a, b, x) = \begin{cases}
   0 & x \leq a \\
   3t^2 - 2t^3 & a < x < b, \quad t = \frac{x-a}{b-a} \\
   1 & x \geq b
   \end{cases}$$

   该函数将 $[0.02, 0.1]$ 区间的 riverNoise 平滑映射到 $[1, 0]$。

3. **侵蚀深度**：$\text{erosion} = 10 \cdot (1 - \text{smoothstep})$ 在河道中心最大（10 方块深），河岸处逐渐减小到 0。

**参数调优**：

| 参数           | 值   | 控制效果                     |
|----------------|------|------------------------------|
| 频率 1.5       | 中频 | 河流弯曲度（频率越高越蜿蜒） |
| Octaves = 4    | 多层 | 河流细节（支流、小溪）       |
| 阈值 [0.02,0.1]| 窄带 | 河流宽度（阈值范围越窄越细） |
| 侵蚀深度 10    | 适中 | 河床深度（值越大河越深）     |

**效果说明**：
- 河流会自然地沿着地形的低洼处流动（因为侵蚀会进一步降低已经较低的地形）。
- 多 octave 的 FBM 使河流呈现自然的蜿蜒形态和支流分布。
- Smoothstep 确保河岸的过渡自然，避免出现陡峭的"峡谷"边缘。

![图2-6: 河流生成算法可视化 - 展示 |FBM| 的热力图和 smoothstep 处理后的河流遮罩]

![图2-7: 最终地形效果 - 展示包含山脉、平原、河流的完整地形俯视图]

---

### 2.4 生物群系系统

生物群系是地形生成中的一个核心概念。本项目根据温度和湿度两个维度，将世界划分为 **8 大生物群系**：

| 群系名称       | 判定条件                         | 地表方块       | 植被特征                     |
|----------------|----------------------------------|----------------|------------------------------|
| Ocean          | 海拔 < 水平面                    | Sand/Gravel    | 无                           |
| Beach          | 海拔 ≈ 水平面±2 且 CloseOcean    | Sand           | 少量棕榈树                    |
| Plains         | 中温中湿 + 低海拔                | Grass          | 零星树木 + 大片混合花田      |
| Forest         | 中高湿度 + 中温                  | Grass          | 高密度橡木 + 稀疏花卉        |
| Desert         | 高温低湿                         | Sand           | 仙人掌 + 枯灌木              |
| Mountains      | 高海拔                           | Stone/Snow     | 岩石峭壁 + 雪顶              |
| SnowyTundra    | 极低温                           | SnowBlock      | 无树木，雪层覆盖             |
| Swamp          | 高湿度 + 低温                    | Grass (暗绿)   | 蓝色兰花 + 浅水坑            |

生物群系的判定逻辑是一个多条件分支结构。系统首先根据海拔高度判断是否为海洋或山脉，同时结合大陆性噪声（Continentalness）来约束沙滩的生成范围（仅在真正的海岸线生成沙滩，而非所有低洼积水区）。然后对于中等海拔的区域，使用温度和湿度的组合来确定具体的群系类型。这种分层判断的方式使代码更清晰，也更容易扩展新的群系。

每个群系不仅决定了地表方块的类型，还影响植被的分布密度和种类。例如，森林群系的树木密度是平原的 3 倍，而沙漠几乎没有植被（只有零星的仙人掌）。

### 2.3 植被生成算法

植被生成是使世界更具生动感的关键。本项目实现了树木和花卉两大类植被，每一类都有多个变种，以增加视觉多样性。

#### 2.3.1 树木生成（变种系统）

本项目实现了 **4 种树形风格**，通过噪声函数随机分配给每棵树：

1. **经典树**：5-7 层立体球形树冠，是最常见的树形。
2. **松树**：窄锥形树冠，叶片层数从下往上递减，常见于寒冷地区。
3. **扁平树**：树冠集中在顶部 1-2 层，呈现热带雨林的特征。
4. **稀疏树**：树冠密度较低，留出缝隙，适合表现老树或枯树。

在实现树木生成时，遇到了一个问题：若树冠完全按照数学公式生成（如球形），视觉效果会显得过于人工化。为解决该问题，本项目给每个叶片方块添加了基于 3D 噪声的随机偏移（±1 格），打破了几何规则感，使树木外观更加自然。

此外，本项目还实现了地面检测逻辑：树木生成前检测周围 3×3 格是否为实体地面，避免树木悬空生成在悬崖边或水面上。

#### 2.3.2 花卉混合分布

在自然界中，花卉通常不会成片地只有一种，而是多种混合生长。为模拟这种效果，本项目使用 **高频噪声 (频率 0.3~0.5)** 在同一区域内混合多种花卉：

```cpp
float flowerMix = glm::perlin(glm::vec2(wx, wz) * 0.4f + seed * 0.3f);
if (flowerMix < 0.2f) {
    setBlock(x, gy + 1, z, BlockId::Poppy);
} else if (flowerMix < 0.4f) {
    setBlock(x, gy + 1, z, BlockId::Dandelion);
} else if (flowerMix < 0.6f) {
    setBlock(x, gy + 1, z, BlockId::RedTulip);
}
```

噪声值被划分为多个区间，每个区间对应一种花卉。由于 Perlin 噪声的连续性，相邻位置的噪声值通常相近，这会形成小范围的"花丛"，但又不会出现大面积的单一品种。频率参数 0.4 控制了花丛的大小——值越大，花丛越小、混合越均匀。

---

## 3. 光照实现

光照是图形学中最能提升视觉效果的技术之一。本项目实现了一套分层的光照系统：底层是预计算的顶点光照（包括环境光遮蔽和面光照），顶层是实时的日夜循环和雾化效果。这种分层设计兼顾了性能和效果。

### 3.1 顶点光照

本项目选择将光照数据"烘焙"到顶点中，在 **Chunk Mesh 构建阶段** 预计算并写入 `RenderVertex.light` 字段。该方案使片元着色器仅需简单地读取并应用该值，避免了逐像素的复杂光照计算。对于体素世界中大量的简单几何体而言，这是性能与效果之间的良好平衡点。

#### 3.1.1 环境光遮蔽

环境光遮蔽 (AO) 是一种模拟间接光照遮挡的技术。其核心思想是：一个点周围的遮挡物越多，到达该点的环境光就越少，因此该点应显得更暗。

在体素世界中，AO 的计算可大大简化。本项目对每个顶点检测其 **3×3 邻域** 中三个特定方向（side1, side2, corner）的遮挡情况：

```cpp
float vertexAO(const glm::ivec3& blockPos, int face, int vertIndex) {
    // 根据面法线和顶点索引确定 side1, side2, corner 方向
    glm::ivec3 side1 = blockPos + sideOffsets[face][vertIndex][0];
    glm::ivec3 side2 = blockPos + sideOffsets[face][vertIndex][1];
    glm::ivec3 corner = blockPos + sideOffsets[face][vertIndex][2];
    
    bool s1 = isSolid(side1);
    bool s2 = isSolid(side2);
    bool c = isSolid(corner);
    
    // 遮挡等级：0~3
    if (s1 && s2) return 0.25f; // 完全遮挡
    int occlusion = (s1 ? 1 : 0) + (s2 ? 1 : 0) + (c ? 1 : 0);
    return 1.0f - occlusion * 0.25f; // 每级遮挡降低 25% 亮度
}
```

该算法的精妙之处在于 `if (s1 && s2) return 0.25f;` 这一行。当两个侧边都被遮挡时，corner 位置一定也会被遮挡（因为它夹在两个侧边之间），此时直接返回最大遮挡值。这一优化避免了一次不必要的 corner 检测，同时也符合物理直觉——完全被包围的角落应为最暗。

**视觉效果**：经过 AO 处理后，方块边缘与角落会自动产生柔和的阴影过渡。特别是在室内场景或洞穴中，这种效果非常明显，能大幅增强场景的立体感和真实感。

#### 3.1.2 面光照

除 AO 外，本项目还实现了基于面法线方向的固定光照系数。该方案模拟了在均匀环境光下，不同朝向的面接收到不同强度光照的效果：

| 面法线方向 | 系数  | 视觉效果           |
|------------|-------|--------------------|
| +Y (Top)   | 1.2   | 顶面最亮（天空光） |
| ±Z (侧面)  | 1.0   | 中等亮度           |
| ±X (侧面)  | 0.92  | 略暗               |
| -Y (Bottom)| 0.7   | 底面最暗           |

这些系数通过观察 Minecraft 原版并结合实际测试调整得到。顶面系数最高（1.2），因其直接面向天空光源；底面系数最低（0.7），因其背向光源且易被遮挡；Z 轴和 X 轴的侧面有细微差异（1.0 vs 0.92），这种不对称性打破了视觉上的单调感。

最终的顶点光照值 = AO 系数 × 面光照系数。这两个因素的组合，让体素场景在没有复杂光照计算的情况下，也能呈现出良好的空间感。

### 3.2 全局动态光照

预计算的光照提供了基础，但要使场景更生动，还需实时的光照变化。在 `block.frag` 中，本项目将预计算的 AO 值与动态的环境光和太阳光合成：

```glsl
// 环境光分量
vec3 ambient = uAmbient * albedo * vLight;

// 漫反射分量（Lambert）
float NdotL = max(dot(vNormal, uSunDir), 0.0);
vec3 diffuse = uSunColor * albedo * NdotL * vLight;

// 最终颜色
vec3 finalColor = ambient + diffuse;
```

关键点在于 `vLight` 参数——它是预计算的 AO 和面光照的乘积，作为衰减因子应用到环境光分量上。

`NdotL`（法线与光线方向的点积）是 Lambert 漫反射模型的核心。当表面正对光源时，`NdotL = 1`，漫反射最强；当表面与光线平行或背对时，`NdotL <= 0`，漫反射为零。

**镜面反射实现**：本项目实际上完整实现了 Blinn-Phong 镜面反射，并引入了 Fresnel 效应增强真实感：

```glsl
vec3 specular = vec3(0.0);
if (ndotl > 0.0) {
    vec3 halfDir = normalize(lightDir + viewDir);
    float ndoth = max(dot(normal, halfDir), 0.0);
    float specTerm = pow(ndoth, shininess);
    float norm = (shininess + 8.0) / (8.0 * kPi);
    float hdotv = max(dot(halfDir, viewDir), 0.0);
    vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - hdotv, 5.0);
    specular = uSunColor * fresnel * (specStrength * specTerm * norm);
}
```

该实现采用 Blinn-Phong 模型计算高光项：通过半角向量 `halfDir`（光线与视线的角平分线）与法线的点积计算高光强度。Fresnel 系数 `F0` 根据材质类型动态调整——水面使用较低的 F0（0.02）以呈现柔和高光，而玻璃使用较高的 F0（0.08）以模拟更强的反射。Schlick 近似公式 `F0 + (1.0 - F0) * pow(1.0 - hdotv, 5.0)` 使掠射角处的反射更强，符合物理规律。

### 3.3 日夜循环系统

为使世界更加生动，本项目实现了完整的日夜循环系统。`World::updateSun` 函数每帧更新太阳的位置和颜色：

```cpp
void World::updateSun(float dt) {
    timeOfDay_ += daySpeed_ * dt; // 0.0 ~ 1.0 循环
    if (timeOfDay_ > 1.0f) timeOfDay_ -= 1.0f;
    
    // 太阳方向（绕 X 轴旋转）
    float angle = timeOfDay_ * glm::two_pi<float>();
    sunDir_ = glm::normalize(glm::vec3(0.25f, glm::sin(angle), glm::cos(angle)));
    
    // 根据高度角调整颜色
    float elevation = sunDir_.y;
    if (elevation > 0.0f) {
        // 白天：正午白色 → 黄昏橙色
        sunColor_ = glm::mix(glm::vec3(1.0f, 0.6f, 0.3f), glm::vec3(1.0f), elevation);
    } else {
        // 夜晚：光照强度归零
        sunColor_ = glm::vec3(0.0f);
    }
    
    // 环境光：深夜深蓝 → 白天天空蓝
    ambientColor_ = glm::mix(glm::vec3(0.02f, 0.04f, 0.08f),
                             glm::vec3(0.35f, 0.43f, 0.54f),
                             glm::clamp(elevation + 0.3f, 0.0f, 1.0f));
}
```

该函数的核心思想是用一个 0~1 的 `timeOfDay_` 变量表示一天中的时间，然后将其映射到太阳的角度、颜色以及环境光的变化上。

**太阳轨迹**：太阳绕 X 轴旋转，从东方升起（+Z），经过头顶（+Y），落入西方（-Z）。Y 分量 `sin(angle)` 决定了太阳的高度角——正值表示白天，负值表示夜晚。

**颜色过渡**：本项目使用 `glm::mix` 函数实现颜色的平滑过渡。在日出日落时（`elevation` 接近 0），太阳呈现暖橙色（1.0, 0.6, 0.3）；在正午（`elevation` 接近 1），太阳呈现白色（1.0, 1.0, 1.0）。该设计模拟了大气散射效应——太阳光在穿过更厚的大气层时，蓝色光被散射掉，只剩下红橙色。

**环境光变化**：深夜的环境光是深蓝色（0.02, 0.04, 0.08），模拟月光和星光；白天的环境光是天空蓝（0.35, 0.43, 0.54），模拟天空的漫射光。`elevation + 0.3` 的偏移使环境光在太阳刚落山时不会立即变暗，模拟了黄昏的渐变过程。

### 3.4 雾化系统

雾化是一种经典的图形学技术，用于柔化渲染距离的边缘，同时增强场景的氛围感。本项目实现了 **指数雾模型**：

```glsl
float fogFactor = exp(-uFogDensity * length(vPos - cameraPos));
vec3 fogColor = mix(uSkyColor * 0.5, uSkyColor, clamp(dot(uSunDir, vec3(0,1,0)) + 0.3, 0.0, 1.0));
finalColor = mix(fogColor, finalColor, fogFactor);
```

指数雾模型的核心公式是 `exp(-density * distance)`，它计算出一个 0~1 的衰减因子。距离越远，因子越小，最终颜色越接近雾的颜色。相比线性雾模型，指数雾在近处几乎透明、远处快速衰减，这种非线性特性更符合大气散射的物理规律。

雾的颜色也会随着日夜变化而调整。白天，雾呈现明亮的天空色；傍晚和夜间，雾逐渐变暗并带有蓝色调。`dot(uSunDir, vec3(0,1,0))` 计算太阳的高度角，用于控制雾色的亮度。

### 3.5 实时阴影映射

为增强场景的真实感和空间感，本项目实现了完整的实时阴影系统。阴影映射是一种广泛使用的实时阴影技术，其核心思想是从光源视角渲染深度图，然后在主渲染阶段判断每个像素是否在阴影中。

#### 3.5.1 阴影贴图生成

首先，系统从太阳方向创建一个正交投影的"光源摄像机"，渲染场景的深度信息到 2048×2048 分辨率的阴影贴图中：

```cpp
constexpr int kShadowMapSize = 2048;

glm::mat4 buildLightSpaceMatrix(const glm::vec3& center, const glm::vec3& sunDir, int renderDistance) {
    float range = static_cast<float>(renderDistance * Chunk::SIZE);
    range = glm::max(range, 64.0f);
    float distance = range * 1.5f;
    glm::vec3 lightDir = glm::normalize(-sunDir);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(lightDir, up)) > 0.95f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::mat4 lightView = glm::lookAt(center - lightDir * distance, center, up);
    glm::mat4 lightProj = glm::ortho(-range, range, -range, range, 0.1f, distance + range);
    return lightProj * lightView;
}
```

光源视图矩阵的范围根据渲染距离动态调整，确保阴影覆盖玩家可见的所有区域。正交投影适用于方向光（如太阳光），因为太阳光线可视为平行光。

#### 3.5.2 PCF 软阴影

简单的阴影映射会产生锯齿状的硬边。本项目使用 **PCF** 技术实现软阴影：

```glsl
float calcShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // 自适应偏移，根据法线与光线夹角调整
    float ndotl = max(dot(normal, lightDir), 0.0);
    float bias = max(0.0006 * (1.0 - ndotl), 0.0008);
    
    // 3×3 PCF 采样
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += projCoords.z - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    return shadow * uShadowStrength;
}
```

PCF 通过对阴影贴图进行 3×3 邻域采样并取平均值，产生柔和的阴影边缘。自适应偏移 `bias` 根据表面法线与光线的夹角动态调整，避免了自阴影伪影（shadow acne）问题。

---

## 4. 纹理与云层实现

纹理是体素世界的视觉基础。在本部分中，本项目实现了纹理图集系统、生物群系染色机制，以及程序化的云层渲染。

### 4.1 纹理图集系统

在现代图形学中，纹理切换是一个相对昂贵的操作。体素世界包含几十种不同的方块，若每种方块使用独立的纹理，渲染时需频繁切换纹理单元，严重影响性能。

本项目的解决方案是将所有方块纹理拼接成一张大的 **纹理图集**。渲染时只需绑定这一张纹理，通过 UV 坐标偏移来采样不同的方块材质。

#### 4.1.1 图集构建流程

`TextureAtlas` 类负责从 Faithful 64x 材质包中加载纹理并构建图集：

```cpp
// 预设纹理列表
std::vector<std::string> textureList = {
    "grass_block_top", "grass_block_side", "dirt", "stone",
    "sand", "water_still", "oak_planks", "oak_log",
    "oak_leaves", "glass", "poppy", "dandelion", ...
};

// 计算最优布局（接近正方形）
int tilesPerRow = (int)std::ceil(std::sqrt(textureList.size()));
int atlasWidth = tilesPerRow * TILE_SIZE;
int atlasHeight = tilesPerRow * TILE_SIZE;

// 逐个加载并拷贝到大纹理
for (int i = 0; i < textureList.size(); ++i) {
    int tx = (i % tilesPerRow) * TILE_SIZE;
    int ty = (i / tilesPerRow) * TILE_SIZE;
    copyTileToAtlas(texData, tx, ty, tilePath);
}
```

构建过程分为三步：首先确定需要加载的纹理列表；然后计算最优的图集布局（本项目选择了接近正方形的布局，以充分利用 GPU 纹理缓存）；最后逐个加载纹理并拷贝到大图集中。

本项目使用 stb_image 库加载 PNG 纹理。Faithful 64x 材质包的每个 tile 是 64×64 像素，整个图集约 30 个纹理，最终尺寸为 384×384 像素。该尺寸对于现代 GPU 而言非常小，可完全放入纹理缓存。

#### 4.1.2 UV 坐标计算

使用纹理图集时，一个常见的问题是**采样出血**：当采样点恰好落在两个 tile 的边界时，双线性插值可能会采样到相邻 tile 的像素，导致视觉瑕疵。

为解决该问题，本项目在计算 UV 坐标时，使采样区域在 tile 边缘**缩进 0.5 像素**：

```cpp
UVRect getUV(const std::string& name) {
    int index = nameToIndex[name];
    float u0 = (index % tilesPerRow + 0.5f / TILE_SIZE) / tilesPerRow;
    float v0 = (index / tilesPerRow + 0.5f / TILE_SIZE) / tilesPerRow;
    float u1 = u0 + (TILE_SIZE - 1.0f) / (tilesPerRow * TILE_SIZE);
    float v1 = v0 + (TILE_SIZE - 1.0f) / (tilesPerRow * TILE_SIZE);
    return {u0, v0, u1, v1};
}
```

`0.5f / TILE_SIZE` 即半个像素的偏移量。对于 64×64 的 tile，该值约为 0.0078。虽然很小，但足以避免采样到相邻像素。同样，UV 的最大值也缩进了 1 像素的宽度（`TILE_SIZE - 1.0f`），确保采样始终在 tile 内部。

### 4.2 生物群系染色

Minecraft 中草地和树叶的颜色会随生物群系变化——森林是翠绿色，沙漠边缘是枯黄色，沼泽是暗绿色。这种效果通过 **纹理染色** 实现：基础纹理是灰度图，运行时乘以一个颜色值来获得最终颜色。

本项目在 `World::sampleTint` 函数中实现了该机制：

```cpp
glm::vec3 World::sampleTint(const glm::vec3& worldPos, BlockId id, int face) {
    if (id == BlockId::Grass) {
        if (face == 2) { // 顶面：使用纯生物群系颜色
            return biomeColor(worldPos);
        } else { // 侧面/底面：强制泥土色 (#866043)
            return glm::vec3(0.525f, 0.376f, 0.263f);
        }
    }
    
    // 其他方块：基础 tint × 生物群系颜色
    glm::vec3 base = registry_.info(id).tint;
    if (registry_.info(id).biomeTint) {
        base *= biomeColor(worldPos);
    }
    return base;
}

// 生物群系颜色计算（基于温度/湿度/海拔）
glm::vec3 World::biomeColor(const glm::vec3& worldPos) const {
    // 同样使用 3D 噪声坐标，保持与地形生成的随机性一致
    glm::vec3 pos = glm::vec3(worldPos.x * 0.0022f, worldPos.z * 0.0022f, seed_ * 0.1337f);

    float temperature = glm::clamp(glm::perlin(pos * 0.8f + 13.7f) * 0.5f + 0.5f, 0.0f, 1.0f);
    float moisture = glm::clamp(glm::perlin(pos * 1.4f - 17.3f) * 0.5f + 0.5f, 0.0f, 1.0f);
    
    glm::vec3 plains(0.475f, 0.753f, 0.353f);  // 森林绿 (121, 192, 90)
    glm::vec3 desert(0.93f, 0.86f, 0.52f);     // 沙漠黄
    glm::vec3 swamp(0.28f, 0.32f, 0.22f);      // 沼泽暗绿
    
    // 湿度混合
    glm::vec3 color = glm::mix(plains, swamp, smoothstep(0.4f, 0.8f, moisture));
    // 温度混合
    color = glm::mix(color, desert, smoothstep(0.5f, 0.9f, temperature));
    
    return glm::clamp(color, 0.0f, 1.0f);
}
```

此处有几个值得注意的设计点：

**草方块的特殊处理**：草方块的顶面使用纯生物群系颜色（因为顶面纹理是灰度图），但侧面需强制使用泥土色（#866043），因为侧面纹理包含了泥土和草的混合。若对侧面也应用生物群系染色，会导致泥土部分也变成绿色，视觉效果不自然。

**颜色混合策略**：本项目定义了三种基准颜色——平原的森林绿、沙漠的枯黄、沼泽的暗绿，然后使用 `smoothstep` 函数在这些颜色之间平滑过渡。`smoothstep(0.4, 0.8, moisture)` 表示当湿度从 0.4 增加到 0.8 时，颜色逐渐从平原绿过渡到沼泽暗绿。这种 Hermite 插值比线性插值更平滑，避免了生物群系边界处颜色的突变。

**噪声参数复用**：生物群系颜色计算使用的温度和湿度噪声，与地形生成时使用的是同一组参数（频率、偏移都相同）。这确保了颜色变化与地形生成保持一致——森林区域（高湿度）的草地确实是深绿色，沙漠区域（高温度）的草地确实是枯黄色。

### 4.3 云层实现

云层是渲染中的一个技术挑战。本项目选择了程序化生成的方式——在 GPU 上实时计算云的形状和密度，而非使用预制的纹理。该方式的优点是云层可无限延伸且无缝衔接，缺点是需在片元着色器中执行复杂的噪声计算。

#### 4.3.1 云层网格生成

首先需要一个承载云层的几何体。`World::CloudLayer` 在 Y=90 高度生成一个 2048×2048 米的平面网格：

```cpp
void CloudLayer::generate() {
    for (int z = 0; z < GRID_SIZE; ++z) {
        for (int x = 0; x < GRID_SIZE; ++x) {
            float wx = (x - GRID_SIZE / 2) * SPACING;
            float wz = (z - GRID_SIZE / 2) * SPACING;
            
            RenderVertex v;
            v.pos = glm::vec3(wx, 90.0f, wz);
            v.material = 2.0f; // 标记为云层材质
            vertices.push_back(v);
        }
    }
}
```

该网格覆盖了 $2048 \times 2048$ 米的范围，足以在任何视角下覆盖整个天空。`material = 2.0f` 标记这些顶点为云层材质，片元着色器据此执行特殊的渲染逻辑。

#### 4.3.2 程序化云量生成 

云层的关键在于程序化噪声。本项目在 `block.frag` 中实现了 **分形布朗运动** 来生成体积云的密度场：

```glsl
// Perlin 噪声近似（3D）
float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f); // 平滑插值
    
    // 8 角插值
    float n000 = hash(i + vec3(0,0,0));
    float n100 = hash(i + vec3(1,0,0));
    // ...（完整实现见源码）
    
    return mix(mix(mix(n000, n100, f.x), ...));
}

// FBM（5 octaves）
float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * noise(p);
        p *= 2.02;
        amp *= 0.55;
    }
    return sum;
}

// 云层着色
if (vMaterial == 2.0) {
    vec2 uv = vUV * 4.0 + uCloudOffset * 0.25 + vec2(0.0, uCloudTime * 0.02);
    float d = fbm(uv) * 0.7 + fbm(uv * 1.7 + 23.1);
    d = smoothstep(0.35, 0.75, d);
    float pulse = 0.85 + 0.15 * sin(uCloudTime * 0.4);
    color = mix(vec3(0.58, 0.68, 0.82), vec3(1.0), d) * pulse;
    alpha = d * 0.65 * uCloudEnabled;
}
```

这段代码的核心是 `fbm` 函数，它叠加了 5 个 octaves 的 2D 噪声来生成复杂的云纹理。以下逐步解释：

**基础噪声函数**：`noise(vec2 p)` 是一个 2D 值噪声实现。它在四个网格顶点上计算哈希值，然后用双线性插值得到任意点的噪声值。`f * f * (3.0 - 2.0 * f)` 是 Hermite 平滑函数，使插值曲线在边界处的导数为零，避免出现明显的网格痕迹。

**分形叠加**：`fbm` 函数将多层噪声叠加在一起。每一层（octave）的频率乘以 2.02（`p *= 2.02`），振幅乘以 0.55（`amp *= 0.55`）。第一层控制云的大致形状，后续层添加细节。5 个 octaves 提供了丰富的细节层次。

**双重 FBM 叠加**：`fbm(uv) * 0.7 + fbm(uv * 1.7 + 23.1)` 叠加了两次 FBM 采样，使用不同的频率和偏移，创造出更复杂的云层纹理变化。

**透明度过渡**：`smoothstep(0.35, 0.75, d)` 使云层边缘有一个柔和的透明度过渡，避免硬边。阈值 0.35-0.75 控制云层的密度分布。

**云层脉动**：`pulse = 0.85 + 0.15 * sin(uCloudTime * 0.4)` 添加了轻微的亮度脉动效果，使云层更具动态感。

**云层移动**：`uCloudOffset` 和 `uCloudTime` 使云层随时间缓慢移动，营造风吹云动的效果。

### 4.4 动物系统 

为使世界更加生动，本项目实现了一套完整的动物渲染系统，包括猪、牛、羊三种动物。每种动物都采用分部件建模，支持独立纹理和行走动画。

#### 4.4.1 分部件网格结构

动物模型采用与 Minecraft 原版一致的分部件设计，包括头部、身体和四条腿：

```cpp
struct AnimalMesh {
    struct PartMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;
        void draw() const;
    };
    
    PartMesh head;
    PartMesh body;
    PartMesh leg;
    
    glm::vec3 headPos{0.0f};
    glm::vec3 bodyPos{0.0f};
    std::array<glm::vec3, 4> legPos{glm::vec3(0.0f)};
};
```

每个部件独立建模为一个立方体（Box），其尺寸根据动物类型动态调整：

| 动物 | 腿高 | 身体尺寸 (W×H×D) | 特征 |
|------|------|------------------|------|
| 猪   | 6    | 10×8×8           | 短腿矮胖 |
| 牛   | 12   | 12×10×8          | 高大健壮 |
| 羊   | 12   | 8×8×8            | 中等体型 |

#### 4.4.2 UV 展开与纹理映射

动物纹理使用 Faithful 64x 材质包中的高分辨率贴图。UV 展开遵循 Minecraft 原版的 ModelBox 布局：

```cpp
static AnimalUVLayout::Box makeModelBoxUV(int texW, int texH, 
                                          float u, float v, 
                                          float w, float h, float d) {
    AnimalUVLayout::Box box;
    // 侧面条带: [left][front][right][back]
    box.front  = quadScaled(texW, texH, u + d,         v + d, w, h);
    box.back   = quadScaled(texW, texH, u + d + w + d, v + d, w, h);
    box.right  = quadScaled(texW, texH, u,             v + d, d, h);
    box.left   = quadScaled(texW, texH, u + d + w,     v + d, d, h);
    // 顶面/底面
    box.top    = quadScaled(texW, texH, u + d,         v,     w, d);
    box.bottom = quadScaled(texW, texH, u + d + w,     v,     w, d);
    return box;
}
```

该函数根据原版 64×32 基准坐标系计算 UV，并自动适配不同分辨率的贴图（16x、32x、64x 等）。

#### 4.4.3 渲染流程

动物使用独立的纹理单元（`uPigTex`、`uCowTex`、`uSheepTex`），通过 `material = 4.0` 标记触发动物着色分支：

```glsl
if (fs_in.material >= 3.5 && fs_in.material < 4.5) {
    vec4 texData;
    if (uAnimalKind == 0) {
        texData = texture(uPigTex, fs_in.uv);
    } else if (uAnimalKind == 1) {
        texData = texture(uCowTex, fs_in.uv);
    } else {
        texData = texture(uSheepTex, fs_in.uv);
    }
    if (texData.a < 0.5) discard;
    
    vec3 albedo = texData.rgb * fs_in.color;
    // 应用完整光照模型（含镜面反射和阴影）
    color = applyLighting(albedo, normal, viewDir, lightDir, ao, 
                          vec3(0.04), 32.0, 0.35, shadow);
}
```

动物渲染使用与方块相同的光照模型，包括 Fresnel 镜面反射和阴影映射，确保视觉风格的一致性。

---

## 5. 输入处理、摄像机与方块交互

一个可交互的体素世界需要响应用户输入。本部分实现了第一人称摄像机控制、基于光线投射的方块选择，以及方块的放置与破坏功能。

### 5.1 摄像机系统

摄像机是玩家观察世界的"眼睛"。本项目实现了一个标准的第一人称自由摄像机，支持 WASD 移动和鼠标旋转。

#### 5.1.1 视图矩阵构建

摄像机的核心是生成**视图矩阵（View Matrix）**，它将世界坐标系变换到摄像机坐标系：

```cpp
class Camera {
private:
    glm::vec3 position_{0, 70, 0};
    glm::vec3 front_{0, 0, -1};
    glm::vec3 up_{0, 1, 0};
    float yaw_{-90.0f};   // 偏航角（绕 Y 轴）
    float pitch_{0.0f};   // 俯仰角（绕 X 轴）
    
public:
    glm::mat4 viewMatrix() const {
        return glm::lookAt(position_, position_ + front_, up_);
    }
    
    glm::mat4 projectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
    }
    
    void rotate(float xoffset, float yoffset) {
        yaw_ += xoffset * sensitivity_;
        pitch_ = glm::clamp(pitch_ + yoffset * sensitivity_, -89.0f, 89.0f);
        
        // 更新 front 向量
        front_.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
        front_.y = sin(glm::radians(pitch_));
        front_.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
        front_ = glm::normalize(front_);
    }
};
```

#### 5.1.2 飞行模式

本项目实现了自由飞行模式，玩家可在空中自由移动，便于快速探索和调试地形。移动控制采用标准的 WASD 方案，配合空格和 Shift 控制垂直方向：

```cpp
void processInput(GLFWwindow* window, Camera& camera, float dt) {
    float speed = 7.5f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)) speed *= 2.0f; // Ctrl 加速
    
    if (glfwGetKey(window, GLFW_KEY_W)) camera.move(Camera::FORWARD, speed * dt);
    if (glfwGetKey(window, GLFW_KEY_S)) camera.move(Camera::BACKWARD, speed * dt);
    if (glfwGetKey(window, GLFW_KEY_A)) camera.move(Camera::LEFT, speed * dt);
    if (glfwGetKey(window, GLFW_KEY_D)) camera.move(Camera::RIGHT, speed * dt);
    if (glfwGetKey(window, GLFW_KEY_SPACE)) camera.move(Camera::UP, speed * dt);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) camera.move(Camera::DOWN, speed * dt);
}
```

移动速度乘以 `dt`（帧间隔时间）是确保运动与帧率无关的标准做法。无论帧率是 30 还是 144，玩家每秒移动的距离都是相同的。按住 Ctrl 键可加速至 2 倍速度，方便快速探索世界。

Tab 键用于在飞行模式和行走模式之间切换，F1 键用于切换鼠标捕获状态，方便操作 ImGui 调试面板。

#### 5.1.3 玩家物理系统

本项目实现了完整的玩家物理系统，包括重力、碰撞检测、跳跃等核心机制：

```cpp
constexpr float kPlayerRadius = 0.3f;
constexpr float kPlayerHeight = 1.8f;
constexpr float kEyeHeight = 1.62f;

struct PlayerState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    bool onGround = false;
};

// 每帧物理更新
float gravity = 20.0f;
float jumpSpeed = 8.5f;
float walkSpeed = 10.5f;

player.velocity.y -= gravity * dt;
if (player.onGround && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
    player.velocity.y = jumpSpeed;
}

resolvePlayerCollisions(world, registry, player, dt);
camera.setPosition(player.position + glm::vec3(0.0f, kEyeHeight, 0.0f));
```

**碰撞检测**：玩家被建模为一个 0.6×1.8 米的轴对齐包围盒（AABB）。系统检测该包围盒与世界中所有固体方块的碰撞，并沿 X、Y、Z 三轴分别解算，实现"滑墙"效果：

```cpp
void resolvePlayerCollisions(const World& world, const BlockRegistry& registry, 
                             PlayerState& player, float dt) {
    auto checkCollision = [&](const glm::vec3& p) {
        int x0 = floorToInt(p.x - kPlayerRadius + kCollisionEps);
        int x1 = floorToInt(p.x + kPlayerRadius - kCollisionEps);
        int y0 = floorToInt(p.y + kCollisionEps);
        int y1 = floorToInt(p.y + kPlayerHeight - kCollisionEps);
        int z0 = floorToInt(p.z - kPlayerRadius + kCollisionEps);
        int z1 = floorToInt(p.z + kPlayerRadius - kCollisionEps);
        return anySolidInRange(world, registry, x0, x1, y0, y1, z0, z1);
    };
    
    // X 轴碰撞解算
    glm::vec3 nextPos = player.position;
    nextPos.x += player.velocity.x * dt;
    if (checkCollision(nextPos)) {
        player.velocity.x = 0;
    } else {
        player.position.x = nextPos.x;
    }
    // Y、Z 轴类似处理...
}
```

**飞行模式**：按 Tab 键可切换至飞行模式，此时重力失效，空格键和 Shift 键分别控制上升和下降。当飞行中落地时自动退出飞行模式。

### 5.2 光线投射 

当玩家想要放置或破坏方块时，需要知道摄像机正在看向哪个方块。这就需要**光线投射（Raycasting）**——从摄像机位置沿着视线方向发射一条射线，检测它与哪个方块相交。本项目将方块交互距离设置为 7 个方块，在该范围内可进行破坏和放置操作。

#### 5.2.1 DDA 算法实现

在体素世界中，光线投射可用 **数字微分分析器（DDA, Digital Differential Analyzer）** 算法高效实现。DDA 的核心思想是：光线穿过体素网格时，会依次跨越 X、Y、Z 三个方向的网格边界。仅需计算光线到下一个边界的距离，选择最近的方向前进，然后检测新到达的体素。

```cpp
// 射线检测距离设为 7 个方块
RayHit hit = world.raycast(camera.position(), camera.forward(), 7.0f);
```

```cpp
RayHit World::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) {
    glm::ivec3 mapPos = glm::floor(origin);
    glm::vec3 deltaDist = glm::abs(glm::vec3(1.0f) / dir);
    glm::ivec3 step = glm::sign(dir);
    
    glm::vec3 sideDist = (glm::sign(dir) * (glm::vec3(mapPos) - origin) + 0.5f) * deltaDist;
    
    for (float dist = 0.0f; dist < maxDist; ) {
        BlockId block = blockAt(mapPos);
        if (block != BlockId::Air) {
            return RayHit{true, mapPos, normal, dist};
        }
        
        // 选择最近的交点推进
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            sideDist.x += deltaDist.x;
            mapPos.x += step.x;
            normal = glm::ivec3(-step.x, 0, 0);
            dist = sideDist.x;
        } else if (sideDist.y < sideDist.z) {
            sideDist.y += deltaDist.y;
            mapPos.y += step.y;
            normal = glm::ivec3(0, -step.y, 0);
            dist = sideDist.y;
        } else {
            sideDist.z += deltaDist.z;
            mapPos.z += step.z;
            normal = glm::ivec3(0, 0, -step.z);
            dist = sideDist.z;
        }
    }
    
    return RayHit{false};
}
```
- `mapPos`：当前检测的体素坐标（整数）。
- `deltaDist`：光线在每个轴方向上穿过一个完整体素所需的"参数距离"。公式 `|1/dir|` 来自参数方程 $P(t) = origin + t \cdot dir$ 的性质。
- `step`：每个轴的前进方向，+1 或 -1。
- `sideDist`：光线从当前位置到下一个体素边界的距离。

主循环的逻辑是：比较三个轴方向的 `sideDist`，选择最小的那个方向前进一步，同时更新 `normal`（记录最后穿过的面的法线方向）。这个法线信息在放置方块时非常重要——新方块应该放在被击中面的外侧。

DDA 算法的时间复杂度是 $O(L)$，其中 $L$ 是光线穿过的体素数量。对于 7 格距离的射线检测，最多只需检测约 21 个体素，效率非常高。

#### 5.2.2 屏幕中心准星

为方便玩家定位放置目标，本项目在 UI 绘制阶段使用 ImGui 的前景绘制层添加了屏幕中心十字准星：

```cpp
// 获取屏幕中心点
ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

// 绘制灰色半透明十字准星
ImU32 crosshairColor = IM_COL32(160, 160, 160, 160);
float halfLen = 8.0f;  // 十字半长
float thickness = 2.0f; // 线宽

ImDrawList* drawList = ImGui::GetForegroundDrawList();
drawList->AddLine(ImVec2(center.x - halfLen, center.y), 
                  ImVec2(center.x + halfLen, center.y), crosshairColor, thickness);
drawList->AddLine(ImVec2(center.x, center.y - halfLen), 
                  ImVec2(center.x, center.y + halfLen), crosshairColor, thickness);
```

准星通过 `ImGui::GetForegroundDrawList()` 绘制，确保其位于 HUD 之上且不被场景遮挡。十字半长为 8 像素、线宽 2 像素、颜色为灰色半透明（RGBA: 160, 160, 160, 160），既清晰可见又不会过于干扰视野。

### 5.3 方块交互

有了光线投射，即可实现方块的放置和破坏。

#### 5.3.1 破坏方块（左键长按）

本项目实现了类似 Minecraft 的"长按破坏"机制，按住左键一段时间（0.35 秒）才能破坏方块：

```cpp
if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
    if (rayHit.hit) {
        breakProgress_ += dt / BREAK_TIME; // 0.35 秒破坏
        
        if (breakProgress_ >= 1.0f) {
            world.removeBlock(rayHit.blockPos);
            breakProgress_ = 0.0f;
        }
        
        // 更新 Shader Uniform 以显示高亮动画
        shader.setVec3("uTargetBlock", rayHit.blockPos);
        shader.setFloat("uBreakProgress", breakProgress_);
    }
} else {
    breakProgress_ = 0.0f;
}
```

`breakProgress_` 是一个 0~1 的进度值，每帧根据时间差 `dt` 累加。当达到 1.0 时，调用 `world.removeBlock` 删除目标方块，同时将进度值传递给 Shader，用于显示破坏动画。

**Shader 中的高亮效果**：

在片元着色器中，系统检测当前像素是否属于被选中的方块，并根据破坏进度添加金黄色高亮：

```glsl
// 检测当前像素是否属于击中方块
if (uTargetActive > 0.5 && material < 2.5) {
    vec3 blockPos = floor(fragPos + vec3(0.001));
    vec3 target = floor(uTargetBlock + vec3(0.001));
    float match = 1.0 - min(length(blockPos - target), 1.0);
    float glow = clamp(match * (0.35 + 0.55 * uBreakProgress), 0.0, 1.0);
    color = mix(color, vec3(1.0, 0.82, 0.45), glow);
}
```

`floor(fragPos)` 将世界坐标转换为方块坐标，然后与 `uTargetBlock`（从 CPU 传入的目标方块坐标）比较。如果匹配，就将原始颜色向金黄色 `vec3(1.0, 0.82, 0.45)` 渐变，渐变强度由 `uBreakProgress` 控制。基础发光强度为 0.35，随着破坏进度增加最高可达 0.9。金黄色的选择既能清晰标识目标方块，又比红色更柔和自然。

#### 5.3.2 放置方块（右键）

放置方块需要知道被击中面的法线方向，这样新方块才能放在正确的位置。本项目实现了完整的方块放置功能，支持放置草地、沙子、花朵等多种方块类型：

```cpp
if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
    if (rayHit.hit) {
        // 在击中面的法线方向放置方块
        glm::ivec3 placePos = rayHit.blockPos + rayHit.normal;
        world.placeBlock(placePos, currentHotbarBlock);
    }
}
```

`rayHit.normal` 是 DDA 算法返回的击中面法线向量。将被击中的方块坐标加上法线，即可得到新方块应放置的位置。例如，若击中某个方块的顶面（法线为 (0, 1, 0)），新方块将放置在其上方。

放置操作会触发目标 Chunk 的 Mesh 重建，系统自动将该 Chunk 标记为"脏"并加入重建队列。若放置的是广告牌类方块（如花朵），MeshBuilder 会自动生成交叉平面几何体，使其呈现自然的植物外观。

### 5.4 热键栏与滚轮切换 

为使玩家可快速切换手中的方块，本项目实现了一个热键栏系统，支持数字键和鼠标滚轮两种切换方式：

```cpp
// 数字键切换
for (int i = 0; i < 9; ++i) {
    if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS) {
        currentHotbarIndex = i;
        currentHotbarBlock = hotbarBlocks[i];
    }
}

// Hotbar 方块列表（含多种花卉）
BlockId hotbarBlocks[] = {
    BlockId::Grass, BlockId::Sand, BlockId::Poppy,
    BlockId::Dandelion, BlockId::BlueOrchid, BlockId::RedTulip,
    BlockId::OxeyeDaisy, BlockId::Cornflower
};
```

数字键 1~9 对应热键栏的各个槽位。当按下某个数字键时，`currentHotbarBlock` 会更新为对应的方块类型，后续的放置操作使用该方块。

#### 5.4.1 鼠标滚轮切换

为兼容鼠标滚轮及触控板的细粒度增量，本项目采用累计 delta + 步进转换的方式：

```cpp
// 滚轮回调：累积滚动量
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    gScrollDelta += static_cast<float>(yoffset);
    // 链式调用旧回调以保持 ImGui 的滚动行为
    if (prevScrollCallback) prevScrollCallback(window, xoffset, yoffset);
}

// 主循环中处理滚轮切换
if (std::abs(gScrollDelta) >= 1.0f) {
    int steps = (gScrollDelta >= 1.0f) ? static_cast<int>(std::floor(gScrollDelta))
                                        : static_cast<int>(std::ceil(gScrollDelta));
    gScrollDelta -= steps;
    
    // 循环切换
    int size = static_cast<int>(hotbarBlocks.size());
    selectedSlot = ((selectedSlot - steps) % size + size) % size;
}
```

当 `gScrollDelta >= 1` 时取 `floor`，当 `<= -1` 时取 `ceil`，将结果映射为切换步数。对负数取模时需额外修正，以实现滚轮上下滚动的循环切换。当鼠标未锁定或 ImGui 需要鼠标时，系统会清空滚轮 delta，避免 UI 滚动时误切换方块。

热键栏包含了草地、沙子及多种花卉（虞美人、蒲公英、蓝色兰花、红色郁金香、滨菊、矢车菊等），可直接放置于世界中。

### 5.5 UI 调试面板 (ImGui)

开发过程中需实时查看各种调试信息。ImGui 是一个优秀的即时模式 UI 库，可用于创建调试面板和显示游戏状态：

```cpp
ImGui::Begin("调试信息");
ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
ImGui::Text("位置: (%.1f, %.1f, %.1f)", camera.position().x, camera.position().y, camera.position().z);
ImGui::Text("Chunk 数量: %d", world.chunkCount());
ImGui::Text("当前方块: %s", blockName[selectedSlot]);

ImGui::Checkbox("显示线框", &wireframeMode);
ImGui::Checkbox("显示 Chunk 边界", &showChunkBounds);
ImGui::Checkbox("启用云层", &cloudsEnabled);

if (ImGui::Button("切换日夜")) {
    world.setDaySpeed(world.daySpeed() > 0 ? 0 : 0.0033f);
}
ImGui::End();
```

调试面板显示了帧率、摄像机位置、已加载的 Chunk 数量、当前选中的方块名称等关键信息。复选框可控制线框模式、Chunk 边界显示、云层开关等功能。这些调试工具在开发过程中有助于快速定位性能问题和视觉 bug。

ImGui 的"即时模式"设计非常适合调试场景——每一帧重新构建整个 UI，无需维护复杂的状态。虽然这种方式效率不如保留模式 UI，但对于调试面板而言完全可满足需求。

### 5.6 菜单与存档操作

为了提供更完善的游戏体验，本项目引入了菜单系统与存档功能。

1. **鼠标模式切换**
   按下 **M 键** 可在"摄像机控制模式"与"菜单模式"之间切换：
   - **摄像机控制模式**：鼠标被锁定并隐藏，移动鼠标控制视角旋转。
   - **菜单模式**：鼠标解锁并显示，可自由移动光标点击 UI 元素，此时摄像机停止旋转。

2. **顶部菜单栏**
   在菜单模式下，屏幕顶部会显示主菜单栏。点击 "Game" 菜单可展开选项：
   - **Save Game**：将当前游戏状态（玩家位置、种子信息）保存至 `saves/` 目录下的数据文件。
   - **Load Game**：读取当前存档文件，恢复玩家位置和世界种子（若种子改变则重新生成地形）。

3. **状态反馈**
   执行存档或读档操作后，屏幕会弹出通知（"Game Saved!" 或 "Game Loaded!"），确认操作成功。

这一系统利用了 ImGui 的菜单组件 (`ImGui::BeginMainMenuBar`) 和弹窗功能，结合 C++ 的文件流 (`std::ofstream`/`std::ifstream`) 实现数据的序列化与反序列化。

---

## 6. 总结与展望

本项目是对计算机图形学知识的一次综合实践。通过从零构建一个体素渲染引擎，深入理解了现代图形学管线的各个环节，也积累了相关工程经验。

### 6.1 技术亮点

回顾整个项目，以下几点是主要的技术亮点：

1. **统一 Shader 管线**：通过 `material` 字段复用单一着色器，显著降低了状态切换的开销。该设计在体素世界中尤为有效，因为大量相邻面片通常属于同一种材质。

2. **分层光照系统**：将 AO 和面光照预计算到顶点中，与实时的日夜循环、Blinn-Phong 镜面反射和 Fresnel 效应相结合。这种分层设计以较低的计算成本获得了良好的视觉效果。

3. **实时阴影映射**：使用 2048×2048 分辨率的阴影贴图，结合 3×3 PCF 软阴影和自适应偏移，实现了高质量的动态阴影效果。

4. **生物群系染色**：草地和树叶颜色随环境动态变化，使世界更加生动。该功能实现的关键是确保噪声参数与地形生成保持一致。

5. **程序化云层**：使用 5 octaves 的 FBM 噪声在 GPU 上实时生成云层，避免了预制纹理的局限性。云层可无限延伸，且支持动态移动和脉动效果。

6. **动物系统**：实现了猪、牛、羊三种动物的分部件建模与渲染，支持行走动画和独立纹理映射。

7. **玩家物理系统**：完整的重力、碰撞检测和跳跃系统，支持行走模式和飞行模式的切换。

### 6.2 性能优化方向

当前实现仍有较大的优化空间。后续计划改进以下几个方面：

- **异步 Chunk 生成**：将地形生成与 Mesh 构建移至工作线程，避免主线程卡顿。C++11 的 `std::async` 或线程池均为可行方案。
- **视锥剔除**：在渲染前检测每个 Chunk 的包围盒是否在视锥内，跳过不可见的 Chunk。该优化可显著减少 Draw Call 数量。
- **LOD 系统**：对远处的 Chunk 使用低精度网格（合并相邻面片），在保持视觉效果的同时减少顶点数量。
- **Greedy Meshing**：当前为每个可见的方块面生成独立的四边形。Greedy Meshing 算法可将相邻的同材质面片合并为更大的多边形，大幅减少顶点数。相关接口已预留，后续将实现该优化。

### 6.3 功能扩展建议

除性能优化外，还规划了一些功能扩展方向：

- **库存与合成**：实现物品栏、工作台等 Minecraft 经典玩法，增强游戏性。
- **存档系统**：将世界数据序列化到磁盘，支持保存和加载。
- **高级渲染**：屏幕空间反射 (SSR)、全局光照 (SSGI)、体积光等效果，进一步提升视觉质量。
- **流水物理**：实现水方块的扩散和流动模拟。

### 6.4 项目收获

通过本项目，对计算机图形学的理解从理论层面深入到了实践层面。编写渲染引擎的过程中，不断在性能和效果之间做取舍，由此理解了工程实践中的权衡艺术。

具体收获包括：
- 理解了 OpenGL 状态机的工作方式，以及尽量减少状态切换的必要性
- 掌握了噪声函数的原理和应用，能够用程序化方法生成自然的地形和纹理
- 学会了如何设计可扩展的架构，使代码易于维护和迭代
- 积累了调试图形程序的经验，掌握了定位和解决渲染问题的方法

---

## 附录：构建与运行

### 依赖版本

| 库        | 版本   | 用途                  |
|-----------|--------|----------------------|
| GLFW      | 3.3+   | 窗口与输入管理        |
| GLM       | 0.9.9+ | 数学库（矩阵/向量）   |
| GLAD      | 4.1    | OpenGL 函数加载器     |
| ImGui     | 1.89+  | UI 调试面板           |
| stb_image | 2.27   | PNG/JPG 纹理加载      |

### 编译命令

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
./mycraft
```

---

**作者**：梁恩睿
