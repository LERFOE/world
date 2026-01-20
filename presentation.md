---
marp: true
theme: default
paginate: true
backgroundColor: #ffffff
style: |
  section {
    font-family: 'Helvetica Neue', Arial, sans-serif;
    justify-content: center;
    padding: 50px;
  }
  h1 {
    color: #2c3e50;
    border-bottom: 2px solid #3498db;
    padding-bottom: 10px;
  }
  h2 {
    color: #34495e;
  }
  p, li {
    font-size: 24px;
    color: #444;
  }
  img {
    box-shadow: 0 4px 12px rgba(0,0,0,0.15);
    border-radius: 6px;
  }
  code {
    background: #f0f0f0;
    padding: 2px 5px;
    border-radius: 4px;
    color: #d35400;
  }
---

<!-- _class: lead -->
# Mycraft: 体素世界渲染引擎
## 计算机图形学期末项目

**汇报人：** [你的名字]
**日期：** 2026年1月19日

---

## 项目概述

本项目基于 **C++17** 和 **OpenGL 4.1** 构建了一个高性能体素渲染引擎。

**核心特性：**
*   **无限地形**：基于 Perlin 噪声与分形布朗运动 (FBM) 的多群系生成。
*   **统一渲染架构**：单 Shader 统一处理不透明/半透明/发光物体。
*   **动态光照**：环境光遮蔽 (AO) + 实时阴影 + 昼夜循环。
*   **高效管理**：基于 ECS 思想的数据结构与延迟网格重建。

---

## 1. 架构设计：模块化分层

系统划分为四个独立的层次，实现解耦与高效协作。

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 30px;">

<div>

*   **输入层**：GLFW 事件 $\rightarrow$ 摄像机状态
*   **逻辑层**：`World::update` 负责区块加载与卸载 (Chunk Loading)
*   **渲染层**：`World::render` 负责 Draw Call 与光照 Uniform 设置
*   **UI 层**：ImGui 实时调试面板

</div>

<div>

```cpp
// 核心模块结构
src/
├── chunk.h/cpp    // 网格构建
├── world.h/cpp    // 场景管理
├── shader.h/cpp   // 着色器管线
└── renderer/      // 渲染核心
```

</div>
</div>

---

## 2. 渲染管线：RenderVertex 设计

使用 **64字节** 的“重型”顶点结构，换取渲染逻辑的统一性与灵活性。

```cpp
struct RenderVertex {
    glm::vec3 pos;      // 位置
    glm::vec3 normal;   // 法线
    glm::vec2 uv;       // 纹理坐标 (u, v)
    glm::vec3 color;    // 生物群系染色 (Grass/Leaves)
    float light;        // 预计算 AO 与面光照 (Baked Lighting)
    float material;     // 材质 ID (0=Block, 1=Water, etc.)
    glm::vec3 anim;     // 动画参数 (用于水面波动)
};
```

---

## 3. 地形生成：多维度噪声

通过叠加不同频率的 **Perlin 噪声** 模拟真实地形特征。

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 20px;">

<div>

**噪声参数：**
*   **温度 (Temperature)**: 频率 0.8
*   **湿度 (Humidity)**: 频率 1.4
*   **大陆性 (Continentalness)**: 频率 0.4
*   **河流侵蚀**: 使用 `abs(noise)` 刻蚀河道

</div>

<div>

![height:350px](screenshots/terrain_overview.png)
<small>图：基于 FBM 生成的山脉与平原</small>

</div>
</div>

---

## 3. 地形生成：生物群系 (Biomes)

根据 **[温度, 湿度]** 二维坐标结合 **[大陆性, 海拔]** 划分为 8 类群系。由于引入了大陆性噪声约束，沙滩仅在真实海岸线生成。

![width:900px](screenshots/biomes_diagram.png)
<small>图：不同生物群系的植被分布（左：沙漠，中：森林，右：雪原）</small>

---

## 4. 光照系统：环境光遮蔽 (AO)

**技术原理**：检测体素顶点周围 3x3 邻域的遮挡情况，计算 `occlusion` 值，烘焙入顶点属性。

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px;">

<div>

![width:450px](screenshots/ao_off.png)
<center>AO 关闭 (画面扁平)</center>

</div>

<div>

![width:450px](screenshots/ao_on.png)
<center>AO 开启 (立体感增强)</center>

</div>
</div>

---

## 4. 光照系统：日夜循环与雾效

**实现细节**：
*   **太阳运动**：计算 `sin(time)` 动态更新光源位置。
*   **颜色渐变**：使用 `mix()` 插值实现日出、正午、日落的天空色变化。
*   **指数雾 (Exponential Fog)**：随距离非线性衰减，平滑视野边界。

![width:800px](screenshots/sunset_fog.png)
<small>图：黄昏时刻的橙色光照与大气雾效</small>

---

## 4. 光照系统：实时阴影 (Shadow Mapping)

采用 **PCF (Percentage-Closer Filtering)** 3x3 采样，实现边缘柔和的动态阴影。

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px;">

<div>

![width:450px](screenshots/shadow_off.png)
<center>不带阴影</center>

</div>

<div>

![width:450px](screenshots/shadow_on.png)
<center>带 PCF 软阴影</center>

</div>
</div>

---

## 5. 纹理系统与细节

*   **纹理图集 (Texture Atlas)**：合并所有方块纹理，避免 State Change。
*   **动态水面**：Shader 中根据时间偏移 UV，模拟波动。
*   **渲染优化**：视锥体剔除 (Frustum Culling) + 面剔除 (Face Culling)。

![width:800px](screenshots/atlas_water.png)
<small>图：纹理细节与水面反射效果</small>

---

## 总结与展望

**项目成果：**
*   构建了完整的体素渲染引擎，包含地形、光照、交互三大核心模块。
*   深入理解了 OpenGL 渲染管线与 GLSL 着色器编程。

**未来改进：**
*   **SSAO**: 屏幕空间环境光遮蔽，替代顶点烘焙 AO。
*   **SSR**: 屏幕空间反射，实现更真实的水面。
*   **多线程生成**: 将 Chunk 生成移至后台线程，消除卡顿。

---

<!-- _class: invert -->
# Q & A
## 感谢观看

