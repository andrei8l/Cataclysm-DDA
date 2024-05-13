project(
    'cataclysm-dda',
    'cpp',
    default_options: [
        'buildtype=debugoptimized',
        'cpp_std=c++17',
        'warning_level=3',
        'werror=true',
        'b_ndebug=if-release',
        'b_pie=true',
    ],
    license: 'CC-BY-SA-3.0',
    version: '0.I',
    meson_version: '>= 0.59',
)

cxx = meson.get_compiler('cpp')

subdir('src')
subdir('tests')
subdir('lang/po')

warn_flags = [
    # '-Wall',     # handled by warning-level
    # '-Wextra',   # handled by warning-level
    '-Wformat-signedness',
    '-Wlogical-op',
    '-Wmissing-declarations',
    '-Wmissing-noreturn',
    '-Wnon-virtual-dtor',
    '-Wold-style-cast',
    '-Woverloaded-virtual',
    '-Wsuggest-override',
    '-Wunused-macros',
    '-Wzero-as-null-pointer-constant',
    # '-Wpedantic,  # handled by warning-level
]
dwarn_flags = [
    #  '-Wno-unknown-warning-option', # replaced by get_supported_arguments()
    '-Wno-error=unused-macros',
    '-Wno-dangling-reference', # can't handle type_id
    '-Wno-c++20-compat', # not ready for this
]
extra_flags = [
    '-fsigned-char', # Explicitly let 'char' to be 'signed char' to fix #18776
]
tool_flags = [
    '-DCATA_IN_TOOL',
]
cflags = [
    warn_flags,
    dwarn_flags,
    extra_flags,
]
ldflags = []
defines = [
    '-DGIT_VERSION', # force GIT_VERSION since meson handles this gracefully
]

exe_name = 'cataclysm'
datadir_cdda = '.'
deps = []
user_dir = get_option('user_dir')
local_dist = user_dir == 'local'
cata_type = get_option('cata_type')
cata_ncurses = cata_type == 'ncurses'
cata_sound = cata_type == 'sound'

deps += dependency('zlib')

if not local_dist
    datadir_cdda = 'share/cataclysm-dda' # hardcoded
    defines += ['-DPREFIX', '-DDATA_DIR_PREFIX']
    configure_file(
        input: 'src/prefix.h.in',
        output: 'prefix.h',
        configuration: {'CMAKE_INSTALL_PREFIX': get_option('prefix')},
    )

    if user_dir == 'home'
        defines += '-DUSE_HOME_DIR'
    elif user_dir == 'xdg'
        defines += '-DUSE_XDG_DIR'
    endif
endif

if get_option('debug') == false
    defines += '-DRELEASE'
endif

if cata_ncurses
    deps += dependency('ncursesw')
    defines += '-DIMTUI'
else
    deps += [
        dependency('sdl2'),
        dependency('SDL2_ttf'),
        dependency('SDL2_image'),
    ]
    defines += '-DTILES'
    exe_name += '-tiles'

    if cata_sound
        deps += dependency('SDL2_mixer')
        defines += '-DSDL_SOUND'
    endif
endif

if get_option('localize')
    defines += '-DLOCALIZE'
endif

if get_option('backtrace')
    backtrace_dep = dependency(
        'libbacktrace',
        fallback: 'backtrace',
        required: false,
    )
    if backtrace_dep.found()
        deps += backtrace_dep
        defines += '-DLIBBACKTRACE'
    endif
    if get_option('debug') == false
        cflags += ['-g1']
    endif
    if host_machine.system() == 'windows'
        deps += cxx.find_library('dbghelp')
    endif
    if not cxx.has_function('backtrace')
        if not backtrace_dep.found()
            execinfo_lib = cxx.find_library(
                'execinfo',
                has_headers: ['execinfo.h'],
                required: false,
            )
            if execinfo_lib.found()
                deps += execinfo_lib
            else
                error(
                    'backtrace on this platform requires libbacktrace or libexecinfo ("-Dbacktrace=true")',
                )
            endif
        endif
    endif
    defines += [
        '-DBACKTRACE',
        '-DCATA_STRING_ID_DEBUGGING',
    ]
    ldflags += '-rdynamic'
endif

if host_machine.system() == 'darwin'
    defines += '-DMACOSX'
endif

add_project_arguments(
    [cxx.get_supported_arguments(cflags), defines],
    language: 'cpp',
)
if cxx.get_id() == 'clang' and get_option('b_pch')
    add_project_arguments(
        ['-Xclang', '-fno-pch-timestamp'],
        language: 'cpp',
    )
endif
add_project_link_arguments(
    cxx.get_supported_link_arguments(ldflags),
    language: 'cpp',
)

version_h = vcs_tag(
    command: [
        'git',
        'describe',
        '--tags',
        '--always',
        '--dirty',
        '--match', '[0-9A-Z]*.[0-9A-Z]*',
        '--match', '"cdda-experimental-*"',
    ],
    input: 'meson/version.h.in',
    output: 'version.h',
    replace_string: '@VERSION@',
)

libversion = static_library(
    'version',
    version_h,
    'src/version.cpp',
    install: false,
)

inc_dirs = [
    include_directories('src/third-party', is_system: true),
    include_directories('src'),
]

libimgui_src = files(
    'src/third-party/imgui/imgui.cpp',
    'src/third-party/imgui/imgui_demo.cpp',
    'src/third-party/imgui/imgui_draw.cpp',
    'src/third-party/imgui/imgui_tables.cpp',
    'src/third-party/imgui/imgui_widgets.cpp',
)
imgui_defines = []
if cata_ncurses
    libimgui_src += [
        'src/third-party/imtui/imtui-impl-text.cpp',
        'src/third-party/imtui/imtui-impl-ncurses.cpp',
    ]
else
    libimgui_src += [
        'src/third-party/imgui/imgui_impl_sdl2.cpp',
        'src/third-party/imgui/imgui_impl_sdlrenderer2.cpp',
    ]
    imgui_defines += ['-DIMGUI_DISABLE_OBSOLETE_KEYIO']
endif
defines += imgui_defines

libimgui = static_library(
    'imgui',
    libimgui_src,
    cpp_args: [
        '-w',
        imgui_defines,
    ],
    install: false,
    include_directories: inc_dirs,
    dependencies: deps,
)

libcataclysm = static_library(
    'cataclysm',
    lib_sources,
    third_party_sources,
    include_directories: inc_dirs,
    link_with: libversion,
    dependencies: deps,
    install: false,
    cpp_pch: 'pch/main-pch.hpp',
)

windres = []
if host_machine.system() == 'windows'
    win = import('windows')
    windres += win.compile_resources('src/resource.rc')
    defines += ['-DUSE_WINMAIN']
endif

cataclysm = executable(
    exe_name,
    exe_sources,
    windres,
    include_directories: inc_dirs,
    link_with: [
        libcataclysm,
        libimgui,
    ],
    dependencies: deps,
    install: true,
    win_subsystem: 'windows',
    cpp_pch: 'pch/main-pch.hpp',
)

test_data_msgfmt = []
if get_option('localize')
    msgfmt = find_program('msgfmt', required: false)
    if msgfmt.found()
        test_data_msgfmt = custom_target(
            'test_data_msgfmt',
            input: 'data/mods/TEST_DATA/lang/po/ru.po',
            output: 'fake',
            command: [
                msgfmt,
                '-f',
                '-o', '@SOURCE_ROOT@/data/mods/TEST_DATA/lang/mo/ru/LC_MESSAGES/TEST_DATA.mo',
                '@INPUT@',
            ],
        )
    endif
endif

test_flags = ['-DCATCH_CONFIG_ENABLE_BENCHMARKING']
if get_option('b_pch')
    test_flags += ['-DCATA_CATCH_PCH']
endif

cata_test = executable(
    'cata_test',
    test_sources,
    test_data_msgfmt,
    include_directories: [
        'tests',
        inc_dirs,
    ],
    link_with: [
        libcataclysm,
        libimgui,
    ],
    dependencies: deps,
    install: local_dist,
    build_by_default: false,
    cpp_args: test_flags,
    cpp_pch: 'tests/pch/tests-pch.hpp',
)

json_formatter_sources = files(
    'src/json.cpp',
    'src/wcwidth.cpp',
    'tools/format/format.cpp',
    'tools/format/format_main.cpp',
)

json_formatter = executable(
    'json_formatter',
    json_formatter_sources,
    include_directories: [
        'tools/format',
        inc_dirs,
    ],
    # name_suffix : 'cgi',
    install: local_dist,
    cpp_args: tool_flags,
)

chkjson_sources = files(
    'src/chkjson/chkjson.cpp',
    'src/json.cpp',
    'src/wcwidth.cpp',
)

chkjson = executable(
    'chkjson',
    chkjson_sources,
    include_directories: [
        'src/chkjson',
        inc_dirs,
    ],
    install: false,
    build_by_default: false,
    cpp_args: tool_flags,
)

object_creator_sources = files(
    'object_creator/collapsing_widget.cpp',
    'object_creator/creator_main.cpp',
    'object_creator/creator_main_window.cpp',
    'object_creator/dual_list_box.cpp',
    'object_creator/fake_spell_listbox.cpp',
    'object_creator/fake_spell_window.cpp',
    'object_creator/flowlayout.cpp',
    'object_creator/item_group_window.cpp',
    'object_creator/listwidget_drag.cpp',
    'object_creator/moc_dual_list_box.cpp',
    'object_creator/moc_fake_spell_listbox.cpp',
    'object_creator/moc_fake_spell_window.cpp',
    'object_creator/mods_selection_window.cpp',
    'object_creator/simple_property_widget.cpp',
    'object_creator/spell_window.cpp',
    'src/messages.cpp',
    'tools/format/format.cpp',
)

qt5_dep = dependency(
    'qt5',
    modules: ['Core', 'Gui', 'Widgets'],
    required: false,
    include_type: 'system',
)
if qt5_dep.found()
    object_creator = executable(
        'object_creator',
        object_creator_sources,
        include_directories: [
            'tools/format',
            inc_dirs,
        ],
        link_with: libcataclysm,
        dependencies: qt5_dep,
        install: false,
        build_by_default: false,
        cpp_args: [
            '-DQT_NO_KEYWORDS',
        ],
        cpp_pch: 'pch/main-pch.hpp',
    )
endif

astyle = find_program('astyle', required: false)
if astyle.found()
    run_target(
        'astyle',
        command: [
            astyle,
            '--options=@SOURCE_ROOT@/.astylerc',
            '-Q',
            '-n',
            '-v', '@SOURCE_ROOT@/src/*.h',
            '@SOURCE_ROOT@/tests/*.h',
            lib_sources,
            exe_sources,
            test_sources,
            json_formatter_sources,
            chkjson_sources,
            # object_creator_sources,
        ],
    )
endif

find = find_program('find', required: false)
if find.found()
    run_target(
        'style-all-json',
        command: [
            find,
            '@SOURCE_ROOT@/data',
            '-name', '*.json',
            '-exec', json_formatter,
            '{}',
            ';',
        ],
    )
endif

test_args = [
    '--min-duration 20',
    '--rng-seed time',
    '--use-colour yes',
    '--option_overrides=24_HOUR:12h,AUTOSAVE:true',
]

test(
    'crafting_skill_gain',
    cata_test,
    args: [
        test_args,
        'crafting_skill_gain',
    ],
    workdir: meson.project_source_root(),
    timeout: 1200,
)

test(
    'slow',
    cata_test,
    args: [
        test_args,
        '[slow] ~crafting_skill_gain',
    ],
    workdir: meson.project_source_root(),
    timeout: 3600,
)

test(
    'normal',
    cata_test,
    args: [
        test_args,
        '~[slow] ~[.]',
    ],
    workdir: meson.project_source_root(),
    timeout: 3600,
)

install_subdir(
    'data',
    install_dir: datadir_cdda,
    exclude_files: [
        'CMakeLists.txt',
    ],
    exclude_directories: [
        'xdg',
    ],
    strip_directory: not local_dist,
)
install_data(
    [
        'LICENSE.txt',
        'LICENSE-OFL-Terminus-Font.txt',
        'README.md',
    ],
    install_dir: datadir_cdda,
)

if not cata_ncurses
    install_subdir('gfx', install_dir: datadir_cdda)
endif

bundle_libs = get_option('bundle_libs')

if host_machine.system() == 'darwin'
    install_data('build-data/osx/Cataclysm.sh', install_dir: 'Contents/MacOS')
    install_data(
        'build-data/osx/AppIcon.icns',
        install_dir: 'Contents/Resources',
    )
    install_data('build-data/osx/Info.plist', install_dir: 'Contents')
    install_data(
        'build-data/osx/DS_Store',
        install_dir: '.',
        rename: '.DS_Store',
    )
    install_data(
        'build-data/osx/dmgback.png',
        install_dir: '.',
        rename: '.background.png',
    )
    if bundle_libs
        meson.add_install_script('meson/osx-framework-bundler.sh', cataclysm)
    endif
endif

if host_machine.system() == 'linux'
    if local_dist
        install_data('cataclysm-launcher', install_dir: get_option('bindir'))
        if bundle_libs
            meson.add_install_script(
                'meson/linux-lib-bundler.sh',
                cataclysm,
                get_option('libdir'),
            )
        endif
    else
        if cata_ncurses
            install_man('doc/cataclysm.6')
        else
            datadir = get_option('datadir')
            install_data(
                'data/xdg/org.cataclysmdda.CataclysmDDA.desktop',
                install_dir: datadir / 'applications',
            )
            install_data(
                'data/xdg/org.cataclysmdda.CataclysmDDA.svg',
                install_dir: datadir / 'icons/hicolor/scalable/apps',
            )
            install_data(
                'data/xdg/org.cataclysmdda.CataclysmDDA.appdata.xml',
                install_dir: datadir / 'appdata',
            )
            install_man('doc/cataclysm-tiles.6')
        endif
    endif
endif

if local_dist
    install_subdir('doc', install_dir: '.')
    install_subdir('tests/data', install_dir: './tests')
endif
