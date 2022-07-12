project(
  'tomie', 'c',
  version : '0.1',
  default_options : [
    # Development settings
    'c_std=c2x',
    'warning_level=3',
    'debug=true',
    'optimization=3',
    'b_lto=true',
  ],
)

liburing_dep = dependency(
  'liburing',
  required : true,
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

echo_exe = executable(
  'echo', [ 'echo.c' ],
  dependencies : [ tomie_dep ],
  build_by_default : false,
)
