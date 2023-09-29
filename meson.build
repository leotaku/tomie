project(
  'tomie', 'c',
  version : '0.1',
  default_options : [
    # Development settings
    'c_std=c2x',
    'warning_level=3',
    'debug=true',
    'optimization=3',
    'b_lto=false',
  ],
)

prefer_static_fix_before_v63 = meson.get_external_property('prefer_static')

liburing_dep = dependency(
  'liburing',
  required : true,
  static : prefer_static_fix_before_v63,
)
sqlite3_dep = dependency(
  'sqlite3',
  required : true,
  static : prefer_static_fix_before_v63,
)

tomie_lib = library(
  'tomie', [ 'tomie.c' ],
  dependencies : [ liburing_dep ],
  pic : true,
  install : true,
)
tomie_dep = declare_dependency(
  link_with : [ tomie_lib ],
)

fullstack_exe = executable(
  'fullstack', [ 'contrib/fullstack/main.c' ],
  dependencies : [ tomie_dep, sqlite3_dep ],
  build_by_default : false,
)
echo_exe = executable(
  'echo', [ 'contrib/echo.c' ],
  dependencies : [ tomie_dep ],
  build_by_default : false,
)

docker_prog = find_program(
  'docker',
  required : false,
)
if docker_prog.found()
  tomie_static = custom_target(
    'tomie_static',
    output : 'static',
    command : [docker_prog, 'build', '@SOURCE_ROOT@', '--output', '@OUTPUT@'],
    env : [ 'DOCKER_BUILDKIT=1' ],
    build_always_stale : true,
    build_by_default : false,
  )
endif
