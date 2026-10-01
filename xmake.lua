add_repositories("switch-repo https://github.com/PoloNX/switch-repo.git")
add_repositories("zeromake-repo https://github.com/zeromake/xrepo.git")


includes("toolchain/*.lua")

add_defines(
    'BRLS_RESOURCES="romfs:/"',
    "YG_ENABLE_EVENTS",
    "STBI_NO_THREAD_LOCALS", 
    "BOREALIS_USE_DEKO3D"
)

add_rules("mode.debug", "mode.release")

add_requires("borealis", {repo = "switch-repo"}, "deko3d", "libcurl", "libarchive", "bzip2", "zlib", "liblzma", "lz4", "libexpat", "libzstd", "nlohmann_json")

local app_version = "1.6.0"

target("UAModDownloader")
    set_kind("binary")
    if not is_plat("cross") then
        return
    end

    set_arch("aarch64")
    add_rules("switch")
    set_toolchains("devkita64")
    set_languages("c++17")

    set_values("switch.name", "UAModDownloader")
    set_values("switch.author", "Pro100Luk")
    set_values("switch.version", app_version)
    add_defines('APP_VERSION="' .. app_version .. '"')
    set_values("switch.romfs", "resources")
    set_values("switch.icon", "resources/icon/icon-256.jpg")

    -- SimpleIniParser
    add_files("lib/ini/source/SimpleIniParser/*.cpp")
    add_includedirs("lib/ini/include")
    add_includedirs("lib/ini/include/SimpleIniParser")

    add_files("source/**.cpp")
    add_files("source/utils/version_check.cpp")
    add_includedirs("include")
    add_packages("borealis", "deko3d", "libcurl", "libarchive", "bzip2", "zlib", "liblzma", "lz4", "libexpat", "libzstd", "nlohmann_json")
    add_cxflags("-UNDEBUG", {force = true})

target("BOTWModDownloader")
    set_kind("binary")
    if not is_plat("cross") then
        return
    end

    set_arch("aarch64")
    add_rules("switch")
    set_toolchains("devkita64")
    set_languages("c++17")

    set_values("switch.name", "BOTW UA Installer")
    set_values("switch.author", "Pro100Luk")
    set_values("switch.version", "1.0.0")
    set_values("switch.romfs", "resources")
    set_values("switch.icon", "resources/icon/botw-icon-256.jpg")

    add_defines("BOTW_BUILD")
    add_defines('APP_VERSION="1.0.0"')

    -- SimpleIniParser
    add_files("lib/ini/source/SimpleIniParser/*.cpp")
    add_includedirs("lib/ini/include")
    add_includedirs("lib/ini/include/SimpleIniParser")

    add_files("source/**.cpp")
    add_includedirs("include")
    add_packages("borealis", "deko3d", "libcurl", "libarchive", "bzip2", "zlib", "liblzma", "lz4", "libexpat", "libzstd", "nlohmann_json")
