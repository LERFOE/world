# World 模块说明报告（world.cpp / world.h）

> 目标：梳理当前 `World` 的代码逻辑、函数依赖与变量关系；解释“树生成为什么在 generateTerrain 和 growTree 两处都有”；指出可改进的工程实践；如果发现**确实的错误**则允许修复，并在本报告记录。

## 1. 总览：World 当前承担的职责

当前 `World` 类同时承担了多类职责（典型的 *God Object*）：

1. **Chunk 生命周期管理**：按相机位置加载/卸载 chunk。
2. **地形与内容生成**：在新建 chunk 时生成地形、植被（树/花/草）、动物。
3. **世界查询/修改接口**：`blockAt`、`placeBlock`、`removeBlock`，以及边界相邻 chunk 的 dirty 标记。
4. **Mesh 构建调度**：维护 `meshQueue_`，控制每帧重建数量。
5. **渲染相关**：
   - 渲染 chunk（solid/transparent）
   - 渲染云层/太阳
   - 渲染动物（并设置 `uModel`/`uAnimalKind`）
6. **时间与环境**：`updateSun` 计算 `sunDir_ / sunColor_ / ambientColor_ / skyColor_ / fogDensity_`。
7. **动物逻辑**：动物生成、简单 AI 更新与渲染。

这种“职责大而全”的写法在 Demo/原型阶段很常见，能快速迭代；代价是：函数间依赖变多、状态变量耦合更紧、修改风险更高。

---

## 2. 运行时主链路（每帧）

### 2.1 入口：main.cpp

- `main.cpp` 创建 `TextureAtlas`、`BlockRegistry`、`Shader`，然后构造 `World`。
- 每帧循环中会：
  1) 处理输入更新相机
  2) 调用 `world.update(camera.position(), dt)`
  3) 设置 shader uniform（view/proj、光照、雾、云偏移等）
  4) 调用 `world.render(...)` / `world.renderTransparent(...)` / `world.renderClouds(...)` / `world.renderSun(...)`

### 2.2 World::update 的职责

`World::update(cameraPos, dt)` 典型按以下顺序：

1. 保存相机位置 `cameraPos_`
2. `updateSun(dt)` 更新环境光照/天空/雾
3. `ensureChunksAround(cameraPos_)` 确保周围 chunk 已创建（缺失则生成）
4. `rebuildMeshes()` 从 `meshQueue_` 中取出 dirty chunk 并 `buildMesh`
5. `cleanupChunks(cameraPos_)` 卸载过远 chunk
6. `clouds_->update(dt)` 更新云层偏移
7. `updateAnimals(dt)` 更新动物 AI

这里的核心点：**世界生成（generateTerrain）不是每帧做**，而是“缺 chunk 时做一次”。

---

## 3. Chunk 生成链路（一次性）

`ensureChunksAround` 在发现某个 `ChunkCoord` 不存在时：

1. `auto chunk = std::make_unique<Chunk>(coord)`
2. `generateTerrain(*chunk)`：填充地形方块，并决定“哪里长树/花/草”
3. `spawnAnimalsForChunk(*chunk)`：在该 chunk 内生成动物实体
4. `meshQueue_.push_back(coord)`：标记该 chunk 需要构建 mesh
5. `chunks_.emplace(coord, std::move(chunk))`

---

## 4. “树生成为什么两处都有？”——这是合理的分层

你看到的“树生成”确实分布在两个函数：

- `generateTerrain(Chunk&)`：**决定是否生成树**（spawn policy / 生成策略）。
  - 它负责：根据噪声/高度/水位/边界/密度/间距，挑选哪些 (x,z) 候选点。
  - 一旦决定生成，就调用 `growTree(...)`。

- `growTree(...)`：**具体怎么放置树的方块**（placement / 生成实现）。
  - 它负责：在 (localX, localZ, baseY) 放 `OakLog`，并在顶部周围放 `OakLeaves`。

这种拆分是“应该的”，原因：

- 生成策略（概率、间距、生态）和放置实现（树形结构）变化频率不同。
- 便于未来扩展：同一策略可调用不同树型（橡树/白桦/松树），或同一树型被多个生物群系调用。

更理想的命名（最佳实践建议）：
- `generateTerrain` 内部把树逻辑提炼成 `trySpawnTreeAtColumn(...)`
- `growTree` 更明确地叫 `placeOakTree(...)` 或 `generateOakTreeStructure(...)`

---

## 5. 关键状态变量与依赖关系（哪些最“耦合”）

### 5.1 Chunk 管理相关

- `chunks_`：世界的 chunk 容器（坐标 → Chunk）。
- `meshQueue_`：待重建 mesh 的队列。
- `renderDistance_`：加载/渲染半径。

依赖点：
- `blockAt` 依赖 `chunks_` 的当前加载状态：未加载的 chunk 直接当空气。
- `Chunk::buildMesh` 通过 `sampler(blockAt)` 读取邻居方块来做面剔除，这使得“mesh 构建”依赖 “world 查询”。

### 5.2 世界参数相关

- `seed_`：影响所有噪声（地形、树密度、随机）。
- `waterLevel_`：影响水面、沙滩，以及树/动物生成条件。

依赖点：
- `generateTerrain` 同时依赖 `seed_` 与 `waterLevel_`。
- `spawnAnimalsForChunk` 同时依赖 `seed_`（通过 `noiseRand`）与 `waterLevel_`。

### 5.3 环境光照相关

- `timeOfDay_ / daySpeed_` 驱动 `updateSun`。
- `sunDir_ / sunColor_ / ambientColor_ / skyColor_ / fogDensity_` 被渲染阶段读取（通过 getter）。

最佳实践建议：
- 把“环境系统”抽成 `Environment`（只负责时间/太阳/雾/天空），World 仅持有并转发。

### 5.4 渲染与逻辑混杂（最大耦合点之一）

World 既维护 OpenGL 资源（云层 VAO/VBO、太阳 mesh、动物 mesh），又做生成与 AI。

最佳实践建议：
- `World` 专注数据与规则。
- `WorldRenderer` 专注 OpenGL 资源、绘制顺序、材质分支。
- 这样 `World` 更易单元测试（不依赖 GL 上下文）。

---

## 6. 当前树生成实现的要点（便于你维护）

### 6.1 generateTerrain 的树策略

- `treeMask`：低频噪声，决定“区域树密度”。
- `density`：将噪声映射到 0..1。
- `treeProb`：把密度映射为每格概率（大约 0.5%..1%）。
- `treeChance`：最终的每格随机命中。
- `inside`：留边界避免树冠被截断（但边界大小应与树冠半径匹配）。
- `spacing`：在方形邻域内检查是否已有树干/树叶，避免扎堆。

### 6.2 growTree 的放置实现

- 树干：从 `groundHeight + 1` 起，向上放 4..6 个 `OakLog`（遇到不可替换方块会提前截断）。
- 树冠：以树顶为中心，多层叶子（当前实现为“方形近似球形”的层级结构）。

---

## 7. 本次发现并修复的问题（不改变正确功能的前提下）

> 这些修复属于“明显错误 / 风险点”，修复目标是让行为更符合注释与常识、并避免越界或覆盖错误；不涉及额外新功能。

1. **world.h 中残留的全局 `struct vec3`**
   - 这是一个公共头文件末尾的全局类型，既无使用点，也容易与 `glm::vec3` 混淆。
   - 风险：污染全局命名空间、潜在误用、降低可读性。
   - 已移除该结构体定义。

2. **generateTerrain 中 spacing 注释与实际逻辑不一致**
   - 注释写“最小曼哈顿距离”，但实际是检查一个 `[-spacing, spacing]` 的方形邻域。
   - 这是文档错误（非逻辑错误），已把注释改为“方形邻域半径”。

3. **growTree 树冠放置缺少 X 方向边界检查 + 无条件覆写方块**
   - 原实现只检查了 Z 边界，没有检查 X 边界；在树靠近 chunk 边缘时存在越界风险。
   - 同时树叶无条件 `setBlock`，可能覆盖树干/石头等，导致不符合预期的形状。
   - 已补充 X 边界检查，并改为仅在可替换方块（空气/草/花/叶）上放置叶子。

---

## 8. 最佳实践建议（不要求你立刻重构，但这是“以后不会痛”的方向）

### 8.1 把 World 按“子系统”拆分（推荐顺序）

1. `ChunkManager`：只负责 chunk 的创建/卸载/查找。
2. `TerrainGenerator`：只负责 `generateTerrain`、`growTree` 等纯数据生成。
3. `MeshScheduler`：只负责 `meshQueue_` 与重建节流策略。
4. `EnvironmentSystem`：只负责 `updateSun` 与环境参数。
5. `AnimalSystem`：只负责 `spawnAnimalsForChunk / updateAnimals`（数据与逻辑），渲染由 Renderer 接。
6. `WorldRenderer`：只负责 GL 资源与 draw。

拆分收益：
- 单元测试可做（尤其 TerrainGenerator）。
- GL 资源不再绑死 World 生命周期（更安全）。
- 修改树/地形不会影响渲染代码。

### 8.2 生成代码的建议

- 生成函数尽量做到 **纯函数风格**（输入 seed/坐标，输出结构），最后统一“写入 chunk”。
- 避免在生成过程中读取/写入造成“先后顺序影响结果”的隐式依赖。
  - 例如：树的 spacing 检查依赖 chunk 当前是否已放树，这会引入顺序相关性。
  - 如果追求更稳定的分布，常见做法是“候选点采样→排序→按距离过滤→写入”。

### 8.3 命名与约定

- `generateTerrain`（策略） vs `growTree`（放置）是正确方向。
- 建议将具体树种写进函数名：`placeOakTree`。
- `noiseRand / gaussian01` 建议集中到 `Random`/`Noise` 工具类，减少 World 的“工具函数膨胀”。

---

## 9. 你现在该怎么读/改 World（建议阅读顺序）

1. `World::update`：理解每帧发生什么。
2. `ensureChunksAround`：理解何时生成 chunk。
3. `generateTerrain`：理解地形/植被生成。
4. `growTree`：理解树结构。
5. `rebuildMeshes` + `Chunk::buildMesh`：理解数据如何进入渲染。
6. `blockAt / setBlockInternal / markNeighborsDirty`：理解编辑世界的路径。
7. `updateSun` + 渲染调用：理解环境如何影响 shader。

---

### 附：关于“是不是应该”这个问题的简短结论

是应该的：
- `generateTerrain` 负责“是否/在哪里生成树”（策略）。
- `growTree` 负责“树长什么样/怎么落方块”（实现）。

真正需要注意的不是“两个函数都出现树相关逻辑”，而是：
- 两者职责边界是否清晰。
- `growTree` 是否做了完善的边界/可替换方块检查。
- `generateTerrain` 的边界预留是否与树冠半径匹配。
