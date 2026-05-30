-- PositionTween component: factory function, returns a fresh instance each call
local function createPositionTween()
    return {
        active  = false,
        startX  = 0,
        startY  = 0,
        targetX = 0,
        targetY = 0,
        visualX = 0,
        visualY = 0,
        clock   = 0,
    }
end
return createPositionTween
