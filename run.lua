-- run.lua: Terminal wrapper/runner for the Luau build of ClassiCube

-- Configuration Options
-- Modes:
--   "PLAY"        - Interactive terminal gameplay (renders to console)
--   "RECORD_ANSI" - Runs and records gameplay ANSI characters to "frames.ansi"
--   "STDOUT_BMP"  - Renders 1 frame, dumps it to stdout as a .bmp image, and exits.
--   "STREAM_PPM"  - Streams binary PPM frames to stdout continuously for GUI viewers.
local MODE = "PLAY"

local DUMP_BMP_FRAMES = false -- Dump all frames as BMPs to "frames/" directory (requires has_io)

-- Resolution for PLAY and RECORD_ANSI modes (if USE_FIXED_RESOLUTION is true)
-- If false, it falls back to 80x24 on sandboxed Luau because of lack of terminal size detection.
local USE_FIXED_RESOLUTION = true
local PLAY_WIDTH = 400
local PLAY_HEIGHT = 400

-- Resolution for STREAM_PPM and STDOUT_BMP modes
local GUI_WIDTH = 400
local GUI_HEIGHT = 400

-- Detect available environment capabilities to handle sandboxed/non-sandboxed Luau CLI
local io = _G.io
local os = _G.os
local math = _G.math
local string = _G.string
local buffer = _G.buffer
local bit32 = _G.bit32
local table = _G.table
local pcall = _G.pcall
local print = _G.print
local tonumber = _G.tonumber
local pairs = _G.pairs

local has_io = (type(io) == "table")
local has_os_execute = (type(os) == "table" and type(os.execute) == "function")

local module = require("./classicube")

local function get_terminal_size()
    if not has_io then return 80, 24 end
    local f = io.popen("stty size 2>/dev/null")
    if not f then return 80, 24 end
    local size = f:read("*a")
    f:close()
    if not size or size == "" then return 80, 24 end
    local rows, cols = size:match("(%d+)%s+(%d+)")
    return tonumber(cols) or 80, tonumber(rows) or 24
end

local function save_bmp(filepath, memory, fb_ptr, width, height)
    local size = 54 + width * height * 4
    local bmp_buf = buffer.create(size)

    -- BMP File Header
    buffer.writeu8(bmp_buf, 0, 0x42) -- 'B'
    buffer.writeu8(bmp_buf, 1, 0x4D) -- 'M'
    buffer.writeu32(bmp_buf, 2, size)
    buffer.writeu32(bmp_buf, 6, 0)
    buffer.writeu32(bmp_buf, 10, 54) -- Offset to pixel data

    -- BMP Info Header (BITMAPINFOHEADER)
    buffer.writeu32(bmp_buf, 14, 40)
    buffer.writei32(bmp_buf, 18, width)
    buffer.writei32(bmp_buf, 22, -height) -- Negative for top-down rows
    buffer.writeu16(bmp_buf, 26, 1)
    buffer.writeu16(bmp_buf, 28, 32)      -- 32-bit (RGBA)
    buffer.writeu32(bmp_buf, 30, 0)       -- BI_RGB (uncompressed)
    buffer.writeu32(bmp_buf, 34, 0)
    buffer.writei32(bmp_buf, 38, 0)
    buffer.writei32(bmp_buf, 42, 0)
    buffer.writeu32(bmp_buf, 46, 0)
    buffer.writeu32(bmp_buf, 50, 0)

    -- Pixel Data: Convert RGBA to BGRA
    local mem_buf = memory[1]
    local dst_offset = 54
    for y = 0, height - 1 do
        local src_row = fb_ptr + y * width * 4
        for x = 0, width - 1 do
            local pixel_offset = src_row + x * 4
            local r = buffer.readu8(mem_buf, pixel_offset)
            local g = buffer.readu8(mem_buf, pixel_offset + 1)
            local b = buffer.readu8(mem_buf, pixel_offset + 2)
            local a = buffer.readu8(mem_buf, pixel_offset + 3)

            buffer.writeu8(bmp_buf, dst_offset, b)
            buffer.writeu8(bmp_buf, dst_offset + 1, g)
            buffer.writeu8(bmp_buf, dst_offset + 2, r)
            buffer.writeu8(bmp_buf, dst_offset + 3, a)
            dst_offset = dst_offset + 4
        end
    end

    local data = buffer.tostring(bmp_buf)
    if filepath then
        if not has_io then return nil end
        local f = io.open(filepath, "wb")
        if f then
            f:write(data)
            f:close()
        end
    else
        return data
    end
end

local SPECIAL_KEYS = {
    [" "] = 93,    -- Space
    ["\n"] = 91,   -- Enter
    ["\r"] = 91,   -- Enter
    ["\x1b"] = 92, -- Escape
    ["\x7f"] = 94, -- Backspace
    ["\x08"] = 94, -- Backspace
    ["\t"] = 95,   -- Tab
}

local ARROW_KEYS = {
    ["A"] = 44, -- Up
    ["B"] = 45, -- Down
    ["D"] = 46, -- Left
    ["C"] = 47, -- Right
}

local function main()
    -- Initialize terminal size
    local cols, rows = get_terminal_size()
    local fb_width, fb_height

    if MODE == "STDOUT_BMP" or MODE == "STREAM_PPM" or DUMP_BMP_FRAMES then
        fb_width = GUI_WIDTH
        fb_height = GUI_HEIGHT
        rows = math.floor(fb_height / 2) + 1
        cols = fb_width
    elseif USE_FIXED_RESOLUTION then
        fb_width = PLAY_WIDTH
        fb_height = PLAY_HEIGHT
        rows = math.floor(fb_height / 2) + 1
        cols = fb_width
    else
        -- Leave one row at the bottom for stats/logs
        fb_width = cols
        fb_height = (rows - 1) * 2
        if fb_height < 10 then fb_height = 10 end
    end

    -- Enable raw non-blocking terminal mode if supported
    if MODE ~= "STDOUT_BMP" and MODE ~= "STREAM_PPM" and has_os_execute then
        os.execute("stty -icanon -echo min 0 time 0")
        if DUMP_BMP_FRAMES then
            os.execute("mkdir -p frames")
        end
    end

    local record_file = nil
    if MODE == "RECORD_ANSI" and has_io then
        record_file = io.open("frames.ansi", "wb")
    end

    if MODE ~= "STDOUT_BMP" and MODE ~= "STREAM_PPM" then
        if has_io then
            io.write("\x1b[2J")   -- Clear screen
            io.write("\x1b[?25l") -- Hide cursor
            io.flush()
        else
            print("\x1b[2J\x1b[?25l") -- Hide cursor and clear screen using print
        end
    end

    -- Instantiate the Luau compiled module
    local instance = module()
    local memory = instance.memory

    instance._initialize()
    instance.wasm_init(fb_width, fb_height)

    if MODE == "STDOUT_BMP" then
        -- Tick a frame to render the game world
        instance.wasm_tick(16)
        local fb_ptr = instance.wasm_get_framebuffer()
        local bmp_data = save_bmp(nil, memory, fb_ptr, fb_width, fb_height)
        if has_io then
            io.write(bmp_data)
            io.flush()
        else
            print(bmp_data)
        end
        return
    elseif MODE == "STREAM_PPM" then
        while true do
            instance.wasm_tick(16)
            local fb_ptr = instance.wasm_get_framebuffer()
            local header = string.format("P6\n%d %d\n255\n", fb_width, fb_height)
            local header_len = string.len(header)
            local ppm_buf = buffer.create(header_len + fb_width * fb_height * 3)
            buffer.writestring(ppm_buf, 0, header)

            local src_offset = fb_ptr
            local dst_offset = header_len
            local mem_buf = memory[1]
            for i = 0, fb_width * fb_height - 1 do
                buffer.writeu8(ppm_buf, dst_offset, buffer.readu8(mem_buf, src_offset))
                buffer.writeu8(ppm_buf, dst_offset + 1, buffer.readu8(mem_buf, src_offset + 1))
                buffer.writeu8(ppm_buf, dst_offset + 2, buffer.readu8(mem_buf, src_offset + 2))
                src_offset = src_offset + 4
                dst_offset = dst_offset + 3
            end
            print(buffer.tostring(ppm_buf))
        end
    end

    local last_log = ""
    local keys_pressed = {} -- map of key -> auto-release time
    local mx, my = math.floor(fb_width / 2), math.floor(fb_height / 2)

    local frame_time = 1 / 30 -- Target 30 FPS
    local last_tick = os.clock()
    local frame_count = 0

    while true do
        local now = os.clock()
        local dt = now - last_tick

        if dt >= frame_time then
            last_tick = now

            -- 1. Check auto-release keys
            for key, release_time in pairs(keys_pressed) do
                if now >= release_time then
                    if key == 119 then
                        instance.wasm_mouse_button(119, 0) -- Release left click
                    else
                        instance.wasm_key_event(key, 0)
                    end
                    keys_pressed[key] = nil
                end
            end

            -- 2. Read all available inputs from stdin if supported
            if has_io then
                while true do
                    local char = io.read(1)
                    if not char or char == "" then
                        break
                    end

                    if char == "\x1b" then
                        -- Check for arrow keys escape sequence
                        local next1 = io.read(1)
                        local next2 = io.read(1)
                        if next1 == "[" and ARROW_KEYS[next2] then
                            local cc_key = ARROW_KEYS[next2]
                            instance.wasm_key_event(cc_key, 1)
                            keys_pressed[cc_key] = now + 0.15

                            -- Also use arrow keys to move virtual mouse in menus
                            local step = 4
                            if cc_key == 44 then my = math.max(0, my - step) end             -- Up
                            if cc_key == 45 then my = math.min(fb_height - 1, my + step) end -- Down
                            if cc_key == 46 then mx = math.max(0, mx - step) end             -- Left
                            if cc_key == 47 then mx = math.min(fb_width - 1, mx + step) end  -- Right
                            instance.wasm_mouse_move(mx, my)
                        else
                            -- Pure escape key
                            local cc_key = SPECIAL_KEYS["\x1b"]
                            instance.wasm_key_event(cc_key, 1)
                            keys_pressed[cc_key] = now + 0.15
                        end
                    elseif SPECIAL_KEYS[char] then
                        local cc_key = SPECIAL_KEYS[char]
                        instance.wasm_key_event(cc_key, 1)
                        keys_pressed[cc_key] = now + 0.15

                        -- Enter also clicks the virtual mouse
                        if char == "\n" or char == "\r" then
                            instance.wasm_mouse_button(119, 1) -- Press CC_MOUSE_L
                            keys_pressed[119] = now + 0.15     -- auto-release in 150ms
                        end
                    else
                        -- Standard letter/digit key
                        local upper_char = string.upper(char)
                        local code = string.byte(upper_char)
                        if (code >= 65 and code <= 90) or (code >= 48 and code <= 57) then
                            instance.wasm_key_event(code, 1)
                            keys_pressed[code] = now + 0.15
                        end
                    end
                end
            end

            -- 3. Tick ClassiCube frame
            local running = instance.wasm_tick(dt * 1000)
            if running == 0 then
                break
            end

            -- 4. Check for logs
            local log_len = instance.wasm_get_log_length()
            if log_len > 0 then
                local log_ptr = instance.wasm_get_log()
                local log_bytes = {}
                for i = 0, log_len - 1 do
                    local b = buffer.readu8(memory[1], log_ptr + i)
                    if b == 0 or b == 10 or b == 13 then break end
                    table.insert(log_bytes, string.char(b))
                end
                local current_log = table.concat(log_bytes)
                if current_log ~= last_log and current_log ~= "" then
                    last_log = current_log
                    -- Print log to the status row at the bottom
                    local status_str = string.format("\x1b[%d;1H\x1b[K\x1b[33mLog: %s\x1b[0m", rows, last_log)
                    if has_io then
                        io.write(status_str)
                    else
                        print(status_str)
                    end
                end
            end

            -- 5. Render the double-resolution framebuffer
            local fb_ptr = instance.wasm_get_framebuffer()
            local parts = {}
            table.insert(parts, "\x1b[H") -- Reset cursor to 1, 1

            local last_r1, last_g1, last_b1 = -1, -1, -1
            local last_r2, last_g2, last_b2 = -1, -1, -1

            local mem_buf = memory[1]

            for y = 0, fb_height - 1, 2 do
                local row_parts = {}
                local top_offset = fb_ptr + y * fb_width * 4
                local bot_offset = fb_ptr + (y + 1) * fb_width * 4

                for x = 0, fb_width - 1 do
                    local pixel_top = top_offset + x * 4
                    local pixel_bot = bot_offset + x * 4

                    local r1 = buffer.readu8(mem_buf, pixel_top)
                    local g1 = buffer.readu8(mem_buf, pixel_top + 1)
                    local b1 = buffer.readu8(mem_buf, pixel_top + 2)

                    local r2 = buffer.readu8(mem_buf, pixel_bot)
                    local g2 = buffer.readu8(mem_buf, pixel_bot + 1)
                    local b2 = buffer.readu8(mem_buf, pixel_bot + 2)

                    if r1 ~= last_r1 or g1 ~= last_g1 or b1 ~= last_b1 or r2 ~= last_r2 or g2 ~= last_g2 or b2 ~= last_b2 then
                        table.insert(row_parts,
                            string.format("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm▄", r1, g1, b1, r2, g2, b2))
                        last_r1, last_g1, last_b1 = r1, g1, b1
                        last_r2, last_g2, last_b2 = r2, g2, b2
                    else
                        table.insert(row_parts, "▄")
                    end
                end
                table.insert(parts, table.concat(row_parts))
                table.insert(parts, "\x1b[0m\n")
                last_r1, last_g1, last_b1 = -1, -1, -1
                last_r2, last_g2, last_b2 = -1, -1, -1
            end

            -- Display rendering stats below the viewport
            table.insert(parts,
                string.format("\x1b[%d;1H\x1b[K\x1b[32mFPS: %.1f | Virtual Mouse: (%d, %d)\x1b[0m", rows - 1, 1 / dt, mx,
                    my))

            local frame_data = table.concat(parts)
            if has_io then
                io.write(frame_data)
                io.flush()
            else
                print(frame_data)
            end

            -- 6. Save frames to files if requested
            if record_file then
                record_file:write(frame_data)
                record_file:flush()
            end

            if DUMP_BMP_FRAMES and has_io then
                local frame_path = string.format("frames/frame_%04d.bmp", frame_count)
                save_bmp(frame_path, memory, fb_ptr, fb_width, fb_height)
                frame_count = frame_count + 1
            end
        end
    end

    if record_file then
        record_file:close()
    end
end

local function cleanup()
    if MODE ~= "STDOUT_BMP" then
        if has_io then
            io.write("\x1b[?25h") -- Show cursor
            io.write("\x1b[0m")   -- Reset styling
        else
            print("\x1b[?25h\x1b[0m")
        end
        if has_os_execute then
            os.execute("stty sane") -- Restore terminal state
        end
        print("\nTerminal state successfully restored.")
    end
end

local ok, err = pcall(main)
cleanup()
if not ok and MODE ~= "STDOUT_BMP" then
    print("Execution Error:", err)
end
