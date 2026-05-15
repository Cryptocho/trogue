-- LÖVE Configuration
-- Trogue ECS Demo

function love.conf(t)
    t.console = true
    t.window.title = "Trogue Demo"
    t.window.width = 640
    t.window.height = 480
    t.window.resizable = true
    t.window.minwidth = 320
    t.window.minheight = 240
    
    -- Disable default love2d console on Windows
    t.window.icon = nil
    
    -- FPS display for debugging
    t.fps = 60
    
    -- No audio needed for this demo
    t.audio.enabled = false
end
