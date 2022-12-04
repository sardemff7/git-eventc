project('git-eventc', 'c',
    version: '10',
    meson_version: '>=0.56.0',
    license: [ 'GPL3+' ],
    default_options: [
        'c_std=gnu11',
        'warning_level=2',
    ],
)
is_unix = host_machine.system() != 'windows'
is_windows = not is_unix
pkgconfig = import('pkgconfig')
gnome = import('gnome')

glib_min_major='2'
glib_min_minor='66'
glib_min_version='.'.join([glib_min_major, glib_min_minor])
eventd_min_version='0.23.90'
libgit2_min_version='0.23.0'

glib = dependency('glib-2.0', version: '>= @0@'.format(glib_min_version))
libeventd = dependency('libeventd', version: '>=@0@'.format(eventd_min_version))
libeventc = dependency('libeventc', version: '>=@0@'.format(eventd_min_version))
libsoup = dependency('libsoup-3.0')

libgit2 = dependency('libgit2', version: '>=@0@'.format(libgit2_min_version), required: get_option('hook') != 'false')
json_glib = dependency('json-glib-1.0', required: get_option('webhook') != 'false')

headers = [
    'locale.h',
    'stdio.h',
]
c_compiler = meson.get_compiler('c')
foreach h : headers
    if not c_compiler.has_header(h)
        error('Header @0@ was not found, but is required'.format(h))
    endif
endforeach

header_conf = configuration_data()
other_conf = configuration_data()
header_conf.set_quoted('PACKAGE_NAME', meson.project_name())
header_conf.set_quoted('PACKAGE_VERSION', meson.project_version())
header_conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())

header_conf.set('GLIB_VERSION_MIN_REQUIRED', '(G_ENCODE_VERSION(@0@,@1@))'.format(glib_min_major, glib_min_minor))
header_conf.set('G_LOG_USE_STRUCTURED', true)

header_conf.set_quoted('SYSCONFDIR', join_paths(get_option('prefix'), get_option('sysconfdir')))
other_conf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

header_conf.set('ENABLE_SYSTEMD', get_option('systemd'))
header_conf.set('GIT_EVENTC_DEBUG_OUTPUT', get_option('debug-output'))

config_h = configure_file(output: 'config.h', configuration: header_conf)

add_project_arguments(
    '-I@0@'.format(meson.project_build_root()),
    language: 'c'
)
flags = [
    '-Wformat=2',
    '-Wno-unused-parameter',
    '-Wno-format-nonliteral',
]
foreach f : flags
    if c_compiler.has_argument(f)
        add_project_arguments(f, language: 'c')
    endif
endforeach

nk_options = [
    'enum=true',
    'format-string=true',
    'git-work-tree=@0@'.format(meson.project_source_root()),
]
nk = subproject('libnkutils', default_options: nk_options)
nk_subproject_options = nk.get_variable('nk_options')
foreach o : nk_options + nk_subproject_options
    if ( o.startswith('git-work-tree=') )
        continue
    elif not nk_options.contains(o) or not nk_subproject_options.contains(o)
        error('You must not change libnkutils options @0@ != @1@'.format('|'.join(nk_options), '|'.join(nk_subproject_options)))
    endif
endforeach
libnkutils = nk.get_variable('libnkutils')

libgit_eventc_lib = static_library('git-eventc', [
        'src/libgit-eventc.h',
        'src/libgit-eventc.c',
    ],
    c_args: [ '-DG_LOG_DOMAIN="libgit-eventc"' ],
    dependencies: [ libsoup, libeventc, libeventd, glib ],
)

libgit_eventc = declare_dependency(link_with: libgit_eventc_lib, include_directories: include_directories('src'), dependencies: [ libsoup, libeventc, libeventd, glib ])

if get_option('hook') != 'false' and libgit2.found()
    executable('git-eventc-post-receive', [
            'src/post-receive.c',
        ],
        c_args: [ '-DG_LOG_DOMAIN="git-eventc-post-receive"' ],
        dependencies: [ libgit2, libnkutils, libgit_eventc ],
        install: true,
    )
endif

if get_option('webhook') != 'false' and json_glib.found()
    libsystemd = []
    if get_option('systemd')
        systemd = dependency('systemd')
        libsystemd = dependency('libsystemd', version: '>= 209')
        systemdsystemunit_install_dir = get_option('systemdsystemunitdir')
        if systemdsystemunit_install_dir == ''
            systemdsystemunit_install_dir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir')
        endif

        configure_file(
            input: 'units/git-eventc-webhook.service.in',
            output: 'git-eventc-webhook.service',
            configuration: other_conf,
            install_dir: systemdsystemunit_install_dir,
        )
    endif
    executable('git-eventc-webhook', [
            'src/webhook.c',
            'src/webhook.h',
            'src/webhook-github.c',
            'src/webhook-github.h',
            'src/webhook-gitlab.c',
            'src/webhook-gitlab.h',
            'src/webhook-travis.c',
            'src/webhook-travis.h',
        ],
        c_args: [ '-DG_LOG_DOMAIN="git-eventc-webhook"' ],
        dependencies: [ libsystemd, json_glib, libnkutils, libgit_eventc ],
        install: true,
    )
endif
test('files', executable('files.test', 'tests/files.c', dependencies: libgit_eventc))
