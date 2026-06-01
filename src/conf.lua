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
    
    -- No custom window icon
    t.window.icon = nil
    
    -- Frame rate cap
    t.fps = 60
    
    -- No audio needed for this demo
    t.audio.enabled = false
end
