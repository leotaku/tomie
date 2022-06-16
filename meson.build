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
  required: true,
)
libsystemd_dep = dependency(
  'libsystemd',
  required: true,
)
tomie_lib = both_libraries(
  'tomie', [ 'tomie.c' ],
  dependencies : [ liburing_dep, libsystemd_dep ],
  pic : true,
  install : true,
)

tomie_dep = declare_dependency(
  link_with : [ tomie_lib ],
)

tomie_exe = executable(
  'tomie', [ 'main.c' ],
  override_options : [ 'b_sanitize=address,undefined' ],
  c_args : [ ],
  dependencies : [ liburing_dep, libsystemd_dep ],
)
