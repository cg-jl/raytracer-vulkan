project('raytracer', 'cpp', default_options : ['cpp_std=c++20'])


imgui_sources = [
  'third-party/imgui/backends/imgui_impl_vulkan.cpp',
  'third-party/imgui/backends/imgui_impl_glfw.cpp',
  'third-party/imgui/imgui.cpp',
  'third-party/imgui/imgui_demo.cpp',
  'third-party/imgui/imgui_draw.cpp',
  'third-party/imgui/imgui_tables.cpp',
  'third-party/imgui/imgui_widgets.cpp'
]

vulkan = dependency('vulkan')
inc_dirs = include_directories('.')
glfw = dependency('glfw3')


executable('raytracer', sources : [
'main.cc',
'instance.cc',
'log.cc',
'application.cc',
'image.cc',
'vulkan_utils.cc',
'threading/unique_signal.cc',
'renderer.cc'
] + imgui_sources,
include_directories : [include_directories('third-party'), inc_dirs, include_directories('third-party/imgui')],
dependencies : [vulkan,  glfw])


