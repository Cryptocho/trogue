-- Actor Component
-- 标记组件：标识可执行动作的实体（主要用于敌人AI）
-- 
-- 用法：
--   - 拥有此组件的实体会被 AI System 处理
--   - Player 实体不应拥有此组件（通过 Player 而非 Actor 区分）
--   - 用于查询所有需要AI决策的实体: world:query({"Actor", "Position"})
--
-- 属性：
--   - moveDelay: 移动延迟（秒），影响AI移动频率
--     （此字段保留用于未来扩展，当前AI使用随机移动）
local ActorComponent = {
    moveDelay = 0,  -- 移动间隔（秒），0表示每帧可移动
}

return ActorComponent
