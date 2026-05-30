-- Tween System
-- Updates visual position interpolation for all active tweening entities
-- outQuad easing: -c * t * (t - 2) + b  =>  easedT = -t * (t - 2)  (b=0,c=1)

local Config = require("src.config")

local TweenSystem = {
    priority = 0,
    name = "TweenSystem",
}

local function easeOutQuad(t)
    return -t * (t - 2)
end

function TweenSystem:init(world)
    self.world = world
end

function TweenSystem:update(world, dt)
    local posTweens = world.components.PositionTween
    if not posTweens then return end

    local toUpdate = {}
    for entityId, pt in pairs(posTweens) do
        if pt.active then
            table.insert(toUpdate, entityId)
        end
    end

    for _, entityId in ipairs(toUpdate) do
        local pt = posTweens[entityId]
        if not pt then goto continue end

        local elapsed = (pt.clock or 0) + dt
        local finished = false

        if elapsed >= Config.MOVE_DURATION then
            elapsed = Config.MOVE_DURATION
            finished = true
        end

        -- outQuad easing in [0..1]
        local t = elapsed / Config.MOVE_DURATION
        local easedT = easeOutQuad(t)

        pt.visualX = pt.startX + (pt.targetX - pt.startX) * easedT
        pt.visualY = pt.startY + (pt.targetY - pt.startY) * easedT
        pt.clock = elapsed

        if finished then
            pt.active = false
            pt.visualX = pt.targetX
            pt.visualY = pt.targetY
            pt.clock = 0
        end

        ::continue::
    end
end

-- Start a tween for an entity whose Position has already been updated
function TweenSystem:startTween(entityId, startX, startY)
    local posComp = self.world.components.Position
    if not posComp or not posComp[entityId] then return end

    local pos = posComp[entityId]
    local pt = self.world.components.PositionTween and self.world.components.PositionTween[entityId]
    if not pt then
        pt = require("src.components.position_tween")()
        self.world:addComponent(entityId, "PositionTween", pt)
    end

    pt.active  = true
    pt.startX  = startX
    pt.startY  = startY
    pt.targetX = pos.x
    pt.targetY = pos.y
    pt.visualX = startX
    pt.visualY = startY
    pt.clock   = 0
end

return TweenSystem
