set_xmakever("3.0.0")
set_project("FalloutChat")
set_version("1.0.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

add_requires("spdlog", "fmt", "ixwebsocket")

target("FalloutChat")
    set_kind("shared")
    set_arch("x64")
    set_filename("FalloutChat.dll")

    add_includedirs("../../CommonLibF4/CommonLibF4/include")
    add_includedirs("../../CommonLibF4/lib/mmio/include")
    add_includedirs("src")
    add_includedirs("include")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_packages("spdlog", "fmt", "ixwebsocket")

    add_linkdirs("../../CommonLibF4/build/windows/x64/release")
    add_links("commonlibf4", "mmio")

    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "_UNICODE")
    add_defines("IXWEBSOCKET_USE_TLS")

    set_pcxxheader("src/PCH.h")

    if is_plat("windows") then
        add_cxflags("/permissive-", "/wd4200", "/wd4201", "/wd4324")
        add_syslinks("advapi32", "bcrypt", "crypt32", "d3d11", "d3dcompiler",
                     "dbghelp", "dxgi", "ole32", "oleaut32",
                     "shell32", "user32", "version", "ws2_32")
    end

    after_build(function(target)
        -- Build TypeScript first (if npm is available)
        local ts_build = os.iorunv("npm", {"run", "build"}, {curdir = path.join(os.scriptdir(), "assets/views")})
        if ts_build ~= 0 then
            cprint("${yellow}warning:${clear} TypeScript build failed (npm might not be available)")
        end

        local deploys = {
            "E:\\Modlists\\Fallen World Alpha 2\\mods\\FalloutChat",
            "D:\\Games\\ModlistDownloads\\mods\\FalloutChat",
        }
        for _, base in ipairs(deploys) do
            if os.isdir(base) then
                local plugins = path.join(base, "F4SE/Plugins")
                local views   = path.join(base, "PrismaUI_F4/views/Interface/FalloutChat")
                os.mkdir(plugins)
                os.mkdir(views)
                os.cp(target:targetfile(), plugins)
                local pdb = path.join(target:targetdir(), target:name() .. ".pdb")
                if os.isfile(pdb) then os.cp(pdb, plugins) end
                os.cp(path.join(os.scriptdir(), "assets/views/chat.html"), views)
                local bundle = path.join(os.scriptdir(), "assets/views/dist/chat-bundle.js")
                if os.isfile(bundle) then
                    os.cp(bundle, views)
                end
                local ini = path.join(os.scriptdir(), "FalloutChat.ini")
                if not os.isfile(path.join(plugins, "FalloutChat.ini")) then
                    os.cp(ini, plugins)
                end
                cprint("${bright green}deploy:${clear} %s", base)
            end
        end
    end)
