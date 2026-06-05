set_xmakever("3.0.0")
set_project("FalloutChat")
set_version("1.0.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

-- Use already-built packages from CommonLibF4 vcpkg, or download with timeout
add_requires("spdlog v1.16.0", {configs = {header_only = false, wchar = true, std_format = true}})
add_requires("fmt", "ixwebsocket", "nlohmann_json")

    local game_ver = os.getenv("PRISMA_TARGET") or "ng"
    if game_ver == "og" then
        includes("../../CommonLibF4")
    else
        includes("../../CommonLibF4")
    end

    target("FalloutChat")
        set_kind("shared")
        set_arch("x64")
        set_filename("FalloutChat.dll")

        add_rules("commonlibf4.plugin", {
            name    = "FalloutChat",
            author  = "NomadsReach",
            version = "1.0.0"
        })

        add_includedirs("src", "include")
        add_files("src/**.cpp")
        add_headerfiles("src/**.h", "include/**.h")
        add_packages("spdlog", "fmt", "ixwebsocket", "nlohmann_json")

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
        try {
            function ()
                local npm_cmd = is_plat("windows") and "npm.cmd" or "npm"
                os.iorunv(npm_cmd, {"run", "build"}, {curdir = path.join(os.scriptdir(), "assets/views")})
            end,
            catch {
                function (err)
                    cprint("${yellow}warning:${clear} TypeScript build failed (npm might not be available)")
                end
            }
        }

        local deploy_path = os.getenv("DEPLOY_PATH")
        local deploys = {}
        if deploy_path and deploy_path ~= "" then
            table.insert(deploys, deploy_path)
        else
            deploys = {
                "E:\\Modlists\\Fallen World Alpha 2\\mods\\FalloutChat",
                "D:\\Games\\ModlistDownloads\\mods\\FalloutChat",
            }
        end
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
